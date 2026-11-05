/* Real worker, QMD scheduler and memory leases; GPU execution is a controlled callback. */
#include "../runtime/mem.c"
#include "../runtime/nvdrv.c"
#include "../runtime/native_worker.c"
#include <assert.h>
static u8 backing[AS_BASE+0x20000];
int g_verbose;
void (*g_ww_touch)(u64);
static u64 gpu,release_gpu;
static unsigned dispatches,allow_dispatch,allow_queue=1,release_calls,fail_release;
static struct { NativeGpuWorker* worker; GpuChannel* channel; u64 qmd; u32 action,lease; } queued;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void pm_init(PMutex* m){ pthread_mutex_init(&m->l,NULL); }
void pm_lock(PMutex* m){ pthread_mutex_lock(&m->l); }
void pm_unlock(PMutex* m){ pthread_mutex_unlock(&m->l); }
u8* plat_vm_reserve(u64 size){ assert(size==AS_SIZE); return backing; }
int plat_vm_commit(u8* p,u64 size){ assert(p>=backing && p+size<=backing+sizeof backing); return 0; }
void plat_vm_decommit(u8* p,u64 size){ (void)p; (void)size; assert(0); }
int gmem_read(u64 address,void* out,u64 size){
  for(u64 offset=0;offset<size;){
    u64 cpu=gmmu_translate(address+offset); assert(cpu);
    u64 n=0x1000-((address+offset)&0xfff); if(n>size-offset) n=size-offset;
    memcpy((u8*)out+offset,GP(cpu),(size_t)n); offset+=n;
  }
  return 0;
}
void gpu_mem_written(u64 address,u64 size){ assert(address>=release_gpu && address+size<=release_gpu+0x1000); }
int gpu_native_queue_qmd(NativeGpuWorker* worker,GpuChannel* channel,u64 address,u32 action,u32 lease){
  if(!allow_queue) return 0;
  assert(!queued.worker); queued.worker=worker; queued.channel=channel;
  queued.qmd=address; queued.action=action; queued.lease=lease; return 1;
}
int gpu_gl_dispatch_native(GpuChannel* channel,const SwrNativeLaunch* launch,SwrNativeRead read,void* context){
  assert(channel && launch->program==gpu+0x1000 && launch->grid[0]==1 && launch->grid[2]==1);
  assert(launch->block[0]==1 && launch->cbuf[0].address==gpu+0x2000 && launch->cbuf[0].size==16);
  u32 words[4]; assert(read(context,launch->cbuf[0].address,words,sizeof words) && words[0]==0xfeedcafe);
  dispatches++; return allow_dispatch;
}
static void run_queued(void){
  assert(queued.worker);
  native_worker_execute(queued.worker,queued.channel,queued.qmd,queued.action,queued.lease);
  memset(&queued,0,sizeof queued);
}
static int test_acquire(void* owner,uint64_t address,uint64_t size,uint32_t* lease){ return nv_native_range_acquire(*(u64*)owner,address,size,lease)==NG_OK; }
static void test_release(void* owner,uint32_t lease){ assert(nv_native_range_release(*(u64*)owner,lease)==NG_OK); }
static int test_read(void* owner,uint64_t address,void* out,uint32_t size){
  u64 cpu; if(nv_native_range_address(*(u64*)owner,address,size,&cpu)!=NG_OK) return 0;
  return gmem_read(address,out,size)==0;
}
static int test_write(void* owner,uint64_t address,uint32_t payload,uint32_t flags){
  u64 cpu; release_calls++; if(fail_release && release_calls==2) return 0;
  assert(nv_native_range_address(*(u64*)owner,address,4,&cpu)==NG_OK);
  if(flags&(1u<<26)) __atomic_fetch_add((u32*)GP(cpu),payload,__ATOMIC_SEQ_CST); else gw32(cpu,payload);
  return 1;
}
static int test_dispatch(void* owner,const SwrNativeLaunch* launch,SwrNativeRead read,void* context){
  (void)owner; GpuChannel channel={0}; return gpu_gl_dispatch_native(&channel,launch,read,context);
}
int main(void){
  mem_init(); pm_init(&g_nvlock); assert(!mem_map(AS_BASE+0x1000,0x9000,MS_NORMAL,PERM_RW));
  u64 owner=nv_native_owner_open(); u32 memory,release_memory;
  gpu=(1ull<<48)|0x10000; release_gpu=0x0400000000ull;
  assert(nv_native_memory_register(owner,AS_BASE+0x1000,0x5000,&memory)==NG_OK);
  assert(nv_native_memory_register(owner,AS_BASE+0x7000,0x1000,&release_memory)==NG_OK);
  assert(nv_native_map(owner,memory,gpu,0,0x5000)==NG_OK);
  assert(nv_native_map(owner,release_memory,release_gpu,0,0x1000)==NG_OK);
  u32* qmd=(u32*)GP(AS_BASE+0x1000); memset(qmd,0,256);
  qmd[12]=qmd[13]=qmd[14]=1; qmd[18]=0x10030; qmd[19]=0x10001;
  qmd[48]=(u32)(gpu+0x1000); qmd[49]=(u32)((gpu+0x1000)>>32);
  qmd[20]=1; qmd[32]=(u32)(gpu+0x2000); qmd[33]=(u32)((gpu+0x2000)>>32)|(1u<<19);
  qmd[24]=(u32)release_gpu; qmd[25]=(u32)(release_gpu>>32)|0x40800000u; qmd[26]=77;
  gw32(AS_BASE+0x3000,0xfeedcafe);
  NativeGpuWorker* worker=native_worker_create(owner); assert(worker);
  assert(nv_native_owner_close(owner)==NG_BUSY);
  GpuChannel channel={0};
  allow_queue=0; assert(native_worker_submit(worker,&channel,gpu,3)==NG_BUSY);
  allow_queue=1; assert(native_worker_submit(worker,&channel,gpu,3)==NG_OK);
  assert(!gr32(AS_BASE+0x7000) && nv_native_unmap(owner,gpu,0x5000)==NG_BUSY);
  run_queued(); assert(dispatches==1 && !gr32(AS_BASE+0x7000));
  assert(nv_native_unmap(owner,gpu,0x5000)==NG_BUSY); /* failed dispatch's transaction is retained */
  allow_dispatch=1; assert(native_worker_submit(worker,&channel,gpu,2)==NG_OK); run_queued();
  assert(dispatches==2 && gr32(AS_BASE+0x7000)==77);
  native_worker_unref(worker);

  /* A partial release failure retains its snapshot and never reruns the shader. */
  SwrNativeQmdBackend backend={test_acquire,test_release,test_read,test_write,test_dispatch};
  SwrNativeQmd* scheduler=swr_native_qmd_create(&backend,&owner); assert(scheduler);
  qmd[28]=(u32)(release_gpu+4); qmd[29]=qmd[25]|(1u<<26); qmd[30]=5;
  gw32(AS_BASE+0x7000,0); gw32(AS_BASE+0x7004,0); fail_release=1; release_calls=0;
  char error[160];
  assert(!swr_native_qmd_signal(scheduler,gpu,3,error,sizeof error));
  assert(dispatches==3 && gr32(AS_BASE+0x7000)==77 && !gr32(AS_BASE+0x7004));
  assert(!swr_native_qmd_signal(scheduler,gpu,3,error,sizeof error)); /* reload while pending */
  qmd[26]=99; qmd[30]=99; fail_release=0;
  assert(swr_native_qmd_signal(scheduler,gpu,2,error,sizeof error));
  assert(dispatches==3 && gr32(AS_BASE+0x7000)==77 && gr32(AS_BASE+0x7004)==5);
  qmd[4]=1u<<14; qmd[29]=0; qmd[26]=88;
  assert(swr_native_qmd_signal(scheduler,gpu,3,error,sizeof error));
  assert(dispatches==3 && gr32(AS_BASE+0x7000)==77); /* dependency still pending */
  assert(swr_native_qmd_signal(scheduler,gpu,7,error,sizeof error));
  assert(dispatches==4 && gr32(AS_BASE+0x7000)==88);
  swr_native_qmd_destroy(scheduler);
  assert(nv_native_unmap(owner,gpu,0x5000)==NG_OK);
  assert(nv_native_unmap(owner,release_gpu,0x1000)==NG_OK);
  assert(nv_native_memory_release(owner,memory)==NG_OK && nv_native_memory_release(owner,release_memory)==NG_OK);
  assert(nv_native_owner_close(owner)==NG_OK);
  puts("Native worker admission, QMD3 execution boundary, dependencies and retry ownership checks passed."); return 0;
}
