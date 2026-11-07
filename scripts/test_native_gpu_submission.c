/* Ampere/NVN2 class dispatch through the actual GPFIFO executor must preserve
   compute and stream-increment ordering. */
#include "../runtime/gpu.c"
#include <assert.h>
u8* g_mem;
volatile int g_cs_any;
static unsigned compute_calls,increments,legacy_calls;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void pm_lock(PMutex* mutex){ pthread_mutex_lock(&mutex->l); }
void pm_unlock(PMutex* mutex){ pthread_mutex_unlock(&mutex->l); }
u64 gmmu_translate(u64 address){ return address>=0x1000 && address<0x4000?address:0; }
u32 gmmu_generation(void){ return 0; }
void cs_mem_need(u64 address,u64 size){ (void)address; (void)size; assert(0); }
void gpu_mem_written(u64 address,u64 size){ (void)address; (void)size; assert(0); }
int r3d_pending_on(void){ return 0; }
void r3d_pending_flush(void){}
void maxwell3d_method(GpuChannel* channel,u32 method,u32 value,int last){ (void)channel;(void)method;(void)value;(void)last; legacy_calls++; }
void compute_method(GpuChannel* channel,u32 method,u32 value,int last){ (void)channel;(void)method;(void)value;(void)last; compute_calls++; }
void kepler_i2m_method(GpuChannel* channel,u32 method,u32 value){ (void)channel;(void)method;(void)value; assert(0); }
void fermi2d_method(GpuChannel* channel,u32 method,u32 value){ (void)channel;(void)method;(void)value; assert(0); }
void dma_method(GpuChannel* channel,u32 method,u32 value){ (void)channel;(void)method;(void)value; assert(0); }
void gpu_flush(GpuChannel* channel){ (void)channel; }
int gpu_async_submit(u64* entries,u32 count,u32 channel,u32 incs,u32 wait_id,u32 wait_value){ (void)entries;(void)count;(void)channel;(void)incs;(void)wait_id;(void)wait_value; return 0; }
int syncpt_wait(u32 id,u32 value,i64 ns){ (void)id;(void)value;(void)ns; assert(0); return 0; }
void syncpt_incr(u32 id){ assert(id==5); increments++; }
void gpu_gl_stream_incr(u32 id){ assert(id==5); increments++; }
void gpu_gl_before_syncwait(void){ assert(0); }
int gpu_gl_hold_reduce(u64 address,u32 op,u32 value,int sign){ (void)address;(void)op;(void)value;(void)sign; assert(0); return 0; }
int gpu_gl_hold_write(u64 address,const void* data,u32 size){ (void)address;(void)data;(void)size; assert(0); return 0; }
u64 plat_ns(void){ assert(0); return 0; }
u64 gpu_ticks(void){ assert(0); return 0; }
void plat_yield(void){ assert(0); }
void plat_sleep_ns(i64 ns){ (void)ns; assert(0); }
int main(void){
  u8 memory[0x4000]={0}; g_mem=memory; pthread_mutex_init(&g_gpulock.l,NULL);
  u32* commands=(u32*)(memory+0x1000);
  commands[0]=(1u<<29)|(1u<<16)|(1u<<13); commands[1]=0xc7c0;
  commands[2]=(1u<<29)|(1u<<16)|(1u<<13)|0xaf; commands[3]=3;
  /* The puller increment remains part of the same submission. */
  commands[4]=(1u<<29)|(1u<<16)|(1u<<13)|0x1d; commands[5]=(5u<<8)|1;
  u64 entry=0x1000|(6ull<<42);
  gpu_submit(&entry,1,5,2,~0u,0);
  assert(increments==3 && compute_calls==1 && !legacy_calls);
  assert(!g_gpu.chan[0]->submission_failed);
  commands[1]=0xb1c0; entry=0x1000|(4ull<<42);
  gpu_submit(&entry,1,5,2,~0u,0);
  assert(increments==5 && compute_calls==2 && !g_gpu.chan[0]->submission_failed);
  free(g_gpu.chan[0]); g_gpu.chan[0]=NULL; g_gpu.nchan=0;
  puts("Native GPFIFO Ampere dispatch preserves compute and stream-increment ordering."); return 0;
}
