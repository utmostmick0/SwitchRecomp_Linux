/* Real host1x queue, fence reservation, VIC composition and surface ownership. */
#ifndef SWR_TEST_H264
#define SWR_NO_H264
#endif
#include "../runtime/nvdrv.c"
#include "../runtime/host1x.c"
#include <assert.h>
u8* g_mem; char g_exedir[1024],g_exename[256];
static unsigned event_signals;
void kevent_signal(KEvent* e){ (void)e; assert(g_mem[16387]==255); __atomic_add_fetch(&event_signals,1,__ATOMIC_SEQ_CST); }
static u8 yplane[4]={16,235,81,145},uvplane[2]={128,128};
static u32 packet[13],commands[12];
static void* extra_submit(void* unused){ (void)unused; return (void*)(uintptr_t)video_submit(1,(u8*)packet,sizeof packet); }
static void drain(void){ pm_lock(&g_video_queue_lock); while(video_pending) pc_wait(&g_video_space,&g_video_queue_lock,-1); pm_unlock(&g_video_queue_lock); }
static void submit_commands(u32 ly,u32 lc,u32 cfg){
 u32 methods[4]={0x720/4,0x724/4,0x708/4,0x300/4},values[4]={ly>>8,lc>>8,cfg>>8,0};
 for(unsigned i=0;i<4;i++){commands[i*3]=0x10100002;commands[i*3+1]=methods[i];commands[i*3+2]=values[i];}
 memcpy(g_mem+1024,commands,sizeof commands);
}
#ifdef SWR_TEST_H264
static void decode_check(const char* path){
 FILE* f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);rewind(f);u8* data=malloc(size);assert(data&&fread(data,1,size,f)==size);fclose(f);
 size_t starts[32],count=0;
 for(size_t i=2;i+1<(size_t)size;i++) if(data[i]==1&&!data[i-1]&&!data[i-2]&&(data[i+1]&31)==9){assert(count<32);starts[count++]=i>=3&&!data[i-3]?i-3:i-2;}
 assert(count>=2&&dec_open());SBufferInfo last={0};unsigned char* planes[3]={0};
 for(size_t i=0;i<count;i++){
  size_t end=i+1<count?starts[i+1]:(size_t)size;SBufferInfo info={0};info.uiInBsTimeStamp=i+1;
  DECODING_STATE state=(*g_dec)->DecodeFrameNoDelay(g_dec,data+starts[i],(int)(end-starts[i]),planes,&info);assert(state==0);
  if(info.iBufferStatus==1){keep_frame(planes,&info);last=info;}
 }
 assert(g_vframes && g_fw==64 && g_fh==48 && g_fy!=planes[0]);
 VicFrame* frame=vic_compose(g_fy,g_fuv,g_fw,g_fh,g_fw,g_fh,0x1f,g_vframes);assert(frame);
 assert(frame->pixels[0]>=245 && frame->pixels[1]<=10 && frame->pixels[2]<=10 && frame->pixels[3]==255);
 unsigned before=g_vframes;last.UsrData.sSystemBuffer.iStride[0]=63;keep_frame(planes,&last);assert(g_vframes==before);
 (*g_dec)->Uninitialize(g_dec);WelsDestroyDecoder(g_dec);g_dec=NULL;
 for(unsigned i=0;i<2;i++)free(vic_frames[i].pixels);free(g_fy);free(g_fuv);free(data);
 printf("OpenH264 decoded %u real pictures; owned NV12, RGBA and invalid-stride checks passed.\n",g_vframes);
}
#endif

int main(int argc,char** argv){
#ifdef SWR_TEST_H264
 if(argc==3 && !strcmp(argv[1],"decode")){decode_check(argv[2]);return 0;}
#endif
 (void)argv;g_mem=calloc(1,65536);assert(g_mem);h_init();pm_init(&g_sync_lk);pc_init(&g_sync_cv);
 for(unsigned i=1;i<=4;i++){nvh[i].addr=i==1?1024:i==2?8192:i==3?16384:24576;nvh[i].size=4096;nvh[i].id=i;nvh[i].alive=1;}nvh_next=5;
 u32 cfg=iova_map(2),ly=iova_map(3),lc=iova_map(4);assert(cfg&&ly&&lc);
 g_fw=2;g_fh=2;g_fy=yplane;g_fuv=uvplane;g_vframes=1;g_ch[1].syncpt=40;
 u64 config=0x1full|(1ull<<32)|(1ull<<46);memcpy(g_mem+8192+0x20,&config,8);
 { u32 luma=1|(1u<<14); memcpy(g_mem+8192+0x28,&luma,4); }
 packet[0]=1;packet[2]=1;packet[3]=1;packet[4]=1;packet[6]=12;packet[7]=40;packet[8]=1;
 submit_commands(ly,lc,cfg);memset(g_mem+16384,0xcc,4096);
 if(argc==2){
  assert(!video_submit(1,(u8*)packet,sizeof packet));assert(syncpt_read(40)==1);assert(g_mem[16384]==0&&g_mem[16388]==255);puts("Synchronous video fallback passed.");return 0;
 }
 KEvent event={0};nvev[0]=(NvEvent){.ev=&event,.id=40,.thresh=1,.armed=1};
 pm_lock(&g_hlk);assert(!video_submit(1,(u8*)packet,sizeof packet));
 assert(packet[12]==1 && syncpt_read(40)==0 && g_mem[16384]==0xcc && event_signals==0);
 memset(g_mem+1024,0,sizeof commands); /* caller pushbuffer can be recycled after snapshot */
 pm_unlock(&g_hlk);assert(syncpt_wait(40,1,1000000000));drain();assert(event_signals==1&&!nvev[0].armed);
 assert(g_mem[16384]==0 && g_mem[16385]==0 && g_mem[16386]==0 && g_mem[16387]==255);
 assert(g_mem[16388]==255 && g_mem[16389]==255 && g_mem[16390]==255 && g_mem[16391]==255);
 submit_commands(ly,lc,cfg);pm_lock(&g_hlk);
 for(unsigned i=0;i<VIDEO_QUEUE_DEPTH;i++){assert(!video_submit(1,(u8*)packet,sizeof packet));assert(packet[12]==i+2);}
 pthread_t producer;assert(!pthread_create(&producer,NULL,extra_submit,NULL));
 plat_sleep_ns(10000000);pm_lock(&g_video_queue_lock);assert(video_pending==VIDEO_QUEUE_DEPTH);pm_unlock(&g_video_queue_lock);
 assert(syncpt_read(40)==1);pm_unlock(&g_hlk);void* result;assert(!pthread_join(producer,&result)&&!result);
 assert(syncpt_wait(40,10,1000000000));drain();
 /* Reusing one complete frame cannot change the previous composition slot. */
 pm_lock(&g_hlk);VicFrame* a=vic_compose(yplane,uvplane,2,2,2,2,0x1f,100);assert(a&&a->pixels[0]==0);
 u8 white[4]={235,235,235,235};VicFrame* b=vic_compose(white,uvplane,2,2,2,2,0x1f,101);assert(b&&b!=a&&b->pixels[0]==255&&a->pixels[0]==0);
 assert(vic_compose(yplane,uvplane,2,2,2,2,0x1f,100)==a);
 config=0x44ull|(1ull<<32)|(1ull<<46);memcpy(g_mem+8192+0x20,&config,8);g_vframes=2;pm_unlock(&g_hlk);
 assert(!video_submit(1,(u8*)packet,sizeof packet));assert(syncpt_wait(40,11,1000000000));drain();
 assert(!memcmp(g_mem+16384,yplane,2)&&!memcmp(g_mem+16384+16,yplane+2,2));assert(!memcmp(g_mem+24576,uvplane,2));
 u32 before=syncpt_read(40);assert(video_submit(1,(u8*)packet,0)==1);packet[5]=0xffffffffu;assert(video_submit(1,(u8*)packet,sizeof packet)==1);assert(syncpt_read(40)==before);
 /* No fence token: ioctl return must still imply complete output writes. */
 packet[5]=0;packet[2]=0;packet[3]=0;
 pm_lock(&g_hlk);yplane[0]=81;g_vframes++;pm_unlock(&g_hlk);
 assert(!video_submit(1,(u8*)packet,28));assert(g_mem[16384]==81&&syncpt_read(40)==before);drain();
 /* 1920-wide NV12 rows are not rounded up to 2048 bytes. */
 u8 wide_y[1920*2],wide_uv[1920];memset(wide_y,81,1920);memset(wide_y+1920,145,1920);memset(wide_uv,128,sizeof wide_uv);
 g_fy=wide_y;g_fuv=wide_uv;g_fw=1920;g_fh=2;g_vframes++;
 config=0x44ull|(1919ull<<32)|(1ull<<46);memcpy(g_mem+8192+0x20,&config,8);
 { u32 luma=1919|(1u<<14),chroma=959;memcpy(g_mem+8192+0x28,&luma,4);memcpy(g_mem+8192+0x2c,&chroma,4); }
 memset(g_mem+16384,0xcc,4096);vic_execute(g_ch[1].regs);
 assert(g_mem[16384]==81 && g_mem[16384+1919]==81 && g_mem[16384+1920]==145 && g_mem[16384+3839]==145);
 assert(g_mem[16384+3840]==0xcc && g_mem[24576+1919]==128);
 for(unsigned i=0;i<2;i++)free(vic_frames[i].pixels);free(g_mem);
 puts("Async video: command snapshots, bounded queue, ordered fences, RGBA/NV12 and dual surfaces passed.");
}
