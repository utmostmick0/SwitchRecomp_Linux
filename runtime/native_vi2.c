/* SwitchRecomp, AGPL-3.0. Retained image -> acquire -> queue -> actual present. */
#include "rt.h"
#include "native_vi2.h"
#include <errno.h>
#include <time.h>
enum { IMAGE_FREE=1,IMAGE_ACQUIRED,IMAGE_QUEUED };
typedef struct {
  u64 gpu,size,ticket,encoded_size;
  u32 id,lease,width,height,pitch,staging_stride,state;
  u8 layout_mode,gob_height_log,gob_depth_log;
} NativeImage;
struct NativeVi2 { u64 owner,next_ticket,completed; u32 refs,next_image; PMutex lock; KEvent* vblank,*completion; NativeImage images[64]; };
typedef struct { NativeVi2* vi; u32 image; u64 ticket; } PresentJob;
int gpu_native_queue_rgba(u64,u32,u32,u32,void (*)(void*,int),void*);
int gpu_native_queue_rgba_pixels(const void*,u32,u32,u32,void (*)(void*,int),void*);
static int g_limit_30fps;
static struct timespec g_last_frame;

void native_vi2_set_30fps_limit(int enable){
  g_limit_30fps=enable!=0;
  if(!g_limit_30fps) g_last_frame=(struct timespec){0,0};
}

static void native_vi2_pace_frame(void){
  if(!g_limit_30fps) return;
  const u64 target=33333333ull;
  struct timespec now;
  if(clock_gettime(CLOCK_MONOTONIC,&now)) return;
  if(g_last_frame.tv_sec || g_last_frame.tv_nsec){
    u64 old=(u64)g_last_frame.tv_sec*1000000000ull+(u64)g_last_frame.tv_nsec;
    u64 cur=(u64)now.tv_sec*1000000000ull+(u64)now.tv_nsec;
    if(cur>=old && cur-old<target){
      u64 sleep_ns=target-(cur-old);
      struct timespec req={(time_t)(sleep_ns/1000000000ull),(long)(sleep_ns%1000000000ull)};
      while(nanosleep(&req,&req) && errno==EINTR){}
    }
  }
  clock_gettime(CLOCK_MONOTONIC,&g_last_frame);
}

static int bl_offset_checked(u32 x_bytes,u32 y,u32 width_bytes,u32 height,u32 bh_log,u32 bd_log,u64* out){
  if(!out || !width_bytes || !height || x_bytes>=width_bytes || y>=height || bh_log>5 || bd_log>5) return 0;
  u64 gob_w=((u64)width_bytes+63)/64, bh=1ull<<bh_log, bd=1ull<<bd_log;
  u64 gx=x_bytes/64, gy=y/8;
  u64 block_y=gy/bh, gob_in=gy%bh, block_size=512*bh*bd;
  u64 z_in=0, groups;
  if(block_y>UINT64_MAX/gob_w || block_y*gob_w>UINT64_MAX-gx) return 0;
  groups=block_y*gob_w+gx;
  if(groups>UINT64_MAX/block_size) return 0;
  u64 off=groups*block_size+(z_in*bh+gob_in)*512;
  u32 xb=x_bytes%64,yb=y%8;
  u64 inner=((xb/32)*256)+(yb/2)*64+(xb/16)*32+(yb%2)*16+(xb%16);
  if(off>UINT64_MAX-inner) return 0;
  off+=inner;
  *out=off; return 1;
}

static int bl_span(u32 width,u32 height,u32 bh_log,u32 bd_log,u64* out){
  if(!out || !width || width>UINT32_MAX/4) return 0;
  u32 wb=width*4; u64 off;
  if(!bl_offset_checked(wb-1,height-1,wb,height,bh_log,bd_log,&off) || off>UINT64_MAX-1) return 0;
  *out=off+1; return 1;
}

static int image_layout_valid(u32 width,u32 height,u32 pitch,u32 mode,u32 bh,u32 bd,u64* encoded){
  if(!width || !height || width>4096 || height>4096 || (pitch&3) || pitch<(u64)width*4) return 0;
  if(mode==NATIVE_VI2_LINEAR){ *encoded=(u64)(height-1)*pitch+(u64)width*4; return 1; }
  return mode==NATIVE_VI2_BLOCK_LINEAR && bl_span(width,height,bh,bd,encoded);
}
static NativeImage* find_image(NativeVi2* vi,u32 id){
  for(unsigned i=0;id && i<64;i++) if(vi->images[i].id==id) return &vi->images[i];
  return NULL;
}
NativeVi2* native_vi2_create(u64 owner){
  if(nv_native_owner_retain(owner)!=NG_OK) return NULL;
  NativeVi2* vi=calloc(1,sizeof *vi);
  if(!vi){ nv_native_owner_drop(owner); return NULL; }
  vi->owner=owner; vi->refs=1; pm_init(&vi->lock); vi->vblank=kevent_new("native vi2 vblank",1); vi->completion=kevent_new("native vi2 frame complete",1); return vi;
}
void native_vi2_ref(NativeVi2* vi){ __atomic_add_fetch(&vi->refs,1,__ATOMIC_RELAXED); }
void native_vi2_unref(NativeVi2* vi){
  if(!vi || __atomic_sub_fetch(&vi->refs,1,__ATOMIC_ACQ_REL)) return;
  /* Every queued frame owns a reference, so none can still use these images. */
  for(unsigned i=0;i<64;i++) if(vi->images[i].id) nv_native_range_release(vi->owner,vi->images[i].lease);
  if(vi->vblank) kobj_unref(&vi->vblank->o);
  if(vi->completion) kobj_unref(&vi->completion->o);
  nv_native_owner_drop(vi->owner);
#ifndef _WIN32
  pthread_mutex_destroy(&vi->lock.l);
#endif
  free(vi);
}
static int native_vi2_register_layout(NativeVi2* vi,u64 gpu,u32 width,u32 height,u32 pitch,u32 mode,u32 bh,u32 bd,u32* out){
  u64 bytes=0;
  if(!vi || !out || (gpu&3) || !image_layout_valid(width,height,pitch,mode,bh,bd,&bytes)) return NG_INVALID;
  pm_lock(&vi->lock); int slot=-1;
  for(unsigned i=0;i<64;i++) if(!vi->images[i].id){ slot=(int)i; break; }
  if(slot<0 || vi->next_image==UINT32_MAX){ pm_unlock(&vi->lock); return NG_LIMIT; }
  u32 lease; int result=nv_native_range_acquire(vi->owner,gpu,bytes,&lease);
  if(result==NG_OK){
    u32 id=++vi->next_image;
    vi->images[slot]=(NativeImage){gpu,bytes,0,bytes,id,lease,width,height,pitch,pitch,IMAGE_FREE,(u8)mode,(u8)bh,(u8)bd}; *out=id;
  }
  pm_unlock(&vi->lock); return result;
}
int native_vi2_register_rgba(NativeVi2* vi,u64 gpu,u32 width,u32 height,u32 pitch,u32* out){ return native_vi2_register_layout(vi,gpu,width,height,pitch,NATIVE_VI2_LINEAR,0,0,out); }
int native_vi2_register_block_linear(NativeVi2* vi,u64 gpu,u32 width,u32 height,u32 pitch,u32 bh,u32 bd,u32* out){ return native_vi2_register_layout(vi,gpu,width,height,pitch,NATIVE_VI2_BLOCK_LINEAR,bh,bd,out); }
int native_vi2_unregister(NativeVi2* vi,u32 id){
  if(!vi) return NG_INVALID;
  pm_lock(&vi->lock); NativeImage* image=find_image(vi,id); int result=NG_NOT_FOUND;
  if(image){ result=NG_BUSY; if(image->state==IMAGE_FREE){
    result=nv_native_range_release(vi->owner,image->lease);
    if(result==NG_OK) memset(image,0,sizeof *image);
  } }
  pm_unlock(&vi->lock); return result;
}
int native_vi2_acquire(NativeVi2* vi,u32 id,u64* ticket){
  if(!vi || !ticket) return NG_INVALID;
  pm_lock(&vi->lock); NativeImage* image=find_image(vi,id); int result=NG_NOT_FOUND;
  if(image){
    result=NG_BUSY;
    if(image->state==IMAGE_FREE){ result=NG_LIMIT;
      if(vi->next_ticket!=UINT64_MAX){ image->ticket=*ticket=++vi->next_ticket; image->state=IMAGE_ACQUIRED; result=NG_OK; }
    }
  }
  pm_unlock(&vi->lock); return result;
}
int native_vi2_cancel(NativeVi2* vi,u32 id,u64 ticket){
  if(!vi || !ticket) return NG_INVALID;
  pm_lock(&vi->lock); NativeImage* image=find_image(vi,id); int result=NG_NOT_FOUND;
  if(image && image->ticket==ticket){ result=NG_BUSY;
    if(image->state==IMAGE_ACQUIRED){ image->state=IMAGE_FREE; image->ticket=0; result=NG_OK; }
  }
  pm_unlock(&vi->lock); return result;
}
static void presented(void* context,int success){
  PresentJob* job=context; NativeVi2* vi=job->vi;
  pm_lock(&vi->lock); NativeImage* image=find_image(vi,job->image);
  if(image && image->state==IMAGE_QUEUED && image->ticket==job->ticket){
    if(success){ image->state=IMAGE_FREE; image->ticket=0; vi->completed++; kevent_signal(vi->completion); }
    else image->state=IMAGE_ACQUIRED;
  }
  pm_unlock(&vi->lock); free(job); native_vi2_unref(vi);
}
int native_vi2_present(NativeVi2* vi,u32 id,u64 ticket,u64 fence,u32 threshold){
  if(!vi || !ticket) return NG_INVALID;
  if(fence){
    if(fence&3) return NG_INVALID;
    u32 lease; int result=nv_native_range_acquire(vi->owner,fence,4,&lease);
    if(result!=NG_OK) return result;
    u64 cpu; result=nv_native_range_address(vi->owner,fence,4,&cpu);
    u32 value=result==NG_OK?__atomic_load_n((u32*)GP(cpu),__ATOMIC_ACQUIRE):0;
    nv_native_range_release(vi->owner,lease);
    if(result!=NG_OK) return result;
    if((i32)(value-threshold)<0) return NG_BUSY;
  }
  PresentJob* job=malloc(sizeof *job); if(!job) return NG_LIMIT;
  pm_lock(&vi->lock); NativeImage* image=find_image(vi,id);
  if(!image || image->ticket!=ticket || image->state!=IMAGE_ACQUIRED){
    pm_unlock(&vi->lock); free(job); return NG_BUSY;
  }
  u64 cpu; int result=nv_native_range_address(vi->owner,image->gpu,image->size,&cpu);
  if(result!=NG_OK){ pm_unlock(&vi->lock); free(job); return result; }
  u32 width=image->width,height=image->height,pitch=image->pitch;
  u8* staging=NULL; u32 submit_pitch=pitch; int host_pixels=0;
  if(image->layout_mode==NATIVE_VI2_BLOCK_LINEAR){
    staging=malloc((size_t)image->staging_stride*height); if(!staging){ pm_unlock(&vi->lock); free(job); return NG_LIMIT; }
    memset(staging,0,(size_t)image->staging_stride*height);
    for(u32 y=0;y<height;y++) for(u32 x=0;x<width*4;x+=16){
      u64 off; u32 n=width*4-x<16?width*4-x:16;
      if(!bl_offset_checked(x,y,width*4,height,image->gob_height_log,image->gob_depth_log,&off) || off+n>image->encoded_size){ free(staging); pm_unlock(&vi->lock); free(job); return NG_INVALID; }
      memcpy(staging+(u64)y*image->staging_stride+x,GP(cpu+off),n);
    }
    submit_pitch=image->staging_stride; host_pixels=1;
  }
  *job=(PresentJob){vi,id,ticket}; image->state=IMAGE_QUEUED; native_vi2_ref(vi);
  pm_unlock(&vi->lock);
  /* No VI lock across enqueue: an immediately completing backend is legal. */
  native_vi2_pace_frame();
  int queued=host_pixels ? gpu_native_queue_rgba_pixels(staging,width,height,submit_pitch,presented,job)
                         : gpu_native_queue_rgba(cpu,width,height,submit_pitch,presented,job);
  if(!queued){
    free(staging);
    presented(job,0); return NG_BUSY;
  }
  free(staging);
  return NG_OK;
}
u64 native_vi2_completed_frames(NativeVi2* vi){
  if(!vi) return 0;
  pm_lock(&vi->lock); u64 value=vi->completed; pm_unlock(&vi->lock); return value;
}
KEvent* native_vi2_vblank_event(NativeVi2* vi){ return vi?vi->vblank:NULL; }
KEvent* native_vi2_completion_event(NativeVi2* vi){ return vi?vi->completion:NULL; }
void native_vi2_signal_vblank(NativeVi2* vi){ if(vi && vi->vblank) kevent_signal(vi->vblank); }
