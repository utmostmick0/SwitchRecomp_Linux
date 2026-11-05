/* SwitchRecomp, AGPL-3.0. Retained image -> acquire -> queue -> actual present. */
#include "rt.h"
#include "native_vi2.h"
enum { IMAGE_FREE=1,IMAGE_ACQUIRED,IMAGE_QUEUED };
typedef struct { u64 gpu,size,ticket; u32 id,lease,width,height,pitch,state; } NativeImage;
struct NativeVi2 { u64 owner,next_ticket,completed; u32 refs,next_image; PMutex lock; NativeImage images[64]; };
typedef struct { NativeVi2* vi; u32 image; u64 ticket; } PresentJob;
int gpu_native_queue_rgba(u64,u32,u32,u32,void (*)(void*,int),void*);
static NativeImage* find_image(NativeVi2* vi,u32 id){
  for(unsigned i=0;id && i<64;i++) if(vi->images[i].id==id) return &vi->images[i];
  return NULL;
}
NativeVi2* native_vi2_create(u64 owner){
  if(nv_native_owner_retain(owner)!=NG_OK) return NULL;
  NativeVi2* vi=calloc(1,sizeof *vi);
  if(!vi){ nv_native_owner_drop(owner); return NULL; }
  vi->owner=owner; vi->refs=1; pm_init(&vi->lock); return vi;
}
void native_vi2_ref(NativeVi2* vi){ __atomic_add_fetch(&vi->refs,1,__ATOMIC_RELAXED); }
void native_vi2_unref(NativeVi2* vi){
  if(!vi || __atomic_sub_fetch(&vi->refs,1,__ATOMIC_ACQ_REL)) return;
  /* Every queued frame owns a reference, so none can still use these images. */
  for(unsigned i=0;i<64;i++) if(vi->images[i].id) nv_native_range_release(vi->owner,vi->images[i].lease);
  nv_native_owner_drop(vi->owner);
#ifndef _WIN32
  pthread_mutex_destroy(&vi->lock.l);
#endif
  free(vi);
}
int native_vi2_register_rgba(NativeVi2* vi,u64 gpu,u32 width,u32 height,u32 pitch,u32* out){
  if(!vi || !out || !width || !height || width>4096 || height>4096 || (gpu|pitch)&3 || pitch<(u64)width*4) return NG_INVALID;
  u64 bytes=(u64)(height-1)*pitch+(u64)width*4;
  pm_lock(&vi->lock); int slot=-1;
  for(unsigned i=0;i<64;i++) if(!vi->images[i].id){ slot=(int)i; break; }
  if(slot<0 || vi->next_image==UINT32_MAX){ pm_unlock(&vi->lock); return NG_LIMIT; }
  u32 lease; int result=nv_native_range_acquire(vi->owner,gpu,bytes,&lease);
  if(result==NG_OK){
    u32 id=++vi->next_image;
    vi->images[slot]=(NativeImage){gpu,bytes,0,id,lease,width,height,pitch,IMAGE_FREE}; *out=id;
  }
  pm_unlock(&vi->lock); return result;
}
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
    if(success){ image->state=IMAGE_FREE; image->ticket=0; vi->completed++; }
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
  *job=(PresentJob){vi,id,ticket}; image->state=IMAGE_QUEUED; native_vi2_ref(vi);
  pm_unlock(&vi->lock);
  /* No VI lock across enqueue: an immediately completing backend is legal. */
  if(!gpu_native_queue_rgba(cpu,width,height,pitch,presented,job)){
    presented(job,0); return NG_BUSY;
  }
  return NG_OK;
}
u64 native_vi2_completed_frames(NativeVi2* vi){
  if(!vi) return 0;
  pm_lock(&vi->lock); u64 value=vi->completed; pm_unlock(&vi->lock); return value;
}
