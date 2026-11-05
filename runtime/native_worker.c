/* SwitchRecomp, AGPL-3.0. Native process worker -> GPU queue -> QMD3 owner. */
#include "gpu.h"
#include "native_gpu.h"
#include "native_qmd.h"
struct NativeGpuWorker {
  u64 owner;
  u32 refs;
  PMutex lock;
  SwrNativeQmd* qmd;
  GpuChannel* channel; /* current GPU-thread execution, serialized by lock */
};
int gpu_gl_dispatch_native(GpuChannel*,const SwrNativeLaunch*,SwrNativeRead,void*);
int gpu_native_queue_qmd(NativeGpuWorker*,GpuChannel*,u64,u32,u32);
static int acquire(void* context,uint64_t address,uint64_t size,uint32_t* token){
  return nv_native_range_acquire(((NativeGpuWorker*)context)->owner,address,size,token)==NG_OK;
}
static void release(void* context,uint32_t token){
  nv_native_range_release(((NativeGpuWorker*)context)->owner,token);
}
static int read_native(void* context,uint64_t address,void* out,uint32_t size){
  NativeGpuWorker* worker=context; u64 cpu;
  if(!out || nv_native_range_address(worker->owner,address,size,&cpu)!=NG_OK) return 0;
  gmem_read(address,out,size); return 1;
}
static int release_qmd(void* context,uint64_t address,uint32_t payload,uint32_t flags){
  NativeGpuWorker* worker=context; u64 cpu;
  if((address&3) || nv_native_range_address(worker->owner,address,4,&cpu)!=NG_OK) return 0;
  if(flags&(1u<<26)) __atomic_fetch_add((u32*)GP(cpu),payload,__ATOMIC_SEQ_CST);
  else __atomic_store_n((u32*)GP(cpu),payload,__ATOMIC_RELEASE);
  gpu_mem_written(address,4); return 1;
}
static int dispatch(void* context,const SwrNativeLaunch* launch,SwrNativeRead read,void* read_context){
  NativeGpuWorker* worker=context;
  return gpu_gl_dispatch_native(worker->channel,launch,read,read_context);
}
NativeGpuWorker* native_worker_create(u64 owner){
  if(nv_native_owner_retain(owner)!=NG_OK) return NULL;
  NativeGpuWorker* worker=calloc(1,sizeof *worker);
  if(!worker){ nv_native_owner_drop(owner); return NULL; }
  worker->owner=owner; worker->refs=1; pm_init(&worker->lock);
  const SwrNativeQmdBackend backend={acquire,release,read_native,release_qmd,dispatch};
  worker->qmd=swr_native_qmd_create(&backend,worker);
  if(!worker->qmd){
    nv_native_owner_drop(owner);
#ifndef _WIN32
    pthread_mutex_destroy(&worker->lock.l);
#endif
    free(worker); return NULL;
  }
  return worker;
}
void native_worker_ref(NativeGpuWorker* worker){ __atomic_add_fetch(&worker->refs,1,__ATOMIC_RELAXED); }
void native_worker_unref(NativeGpuWorker* worker){
  if(!worker || __atomic_sub_fetch(&worker->refs,1,__ATOMIC_ACQ_REL)) return;
  swr_native_qmd_destroy(worker->qmd); nv_native_owner_drop(worker->owner);
#ifndef _WIN32
  pthread_mutex_destroy(&worker->lock.l);
#endif
  free(worker);
}
int native_worker_submit(NativeGpuWorker* worker,GpuChannel* channel,u64 qmd,u32 action){
  if(!worker || !channel || (qmd&0xff) || qmd>=(1ull<<49)) return NG_INVALID;
  switch(action&15){ case 0:case 1:case 2:case 3:case 7:case 12:break;default:return NG_UNSUPPORTED; }
  u32 lease; int result=nv_native_range_acquire(worker->owner,qmd,256,&lease);
  if(result!=NG_OK) return result;
  native_worker_ref(worker);
  if(!gpu_native_queue_qmd(worker,channel,qmd,action,lease)){
    nv_native_range_release(worker->owner,lease); native_worker_unref(worker); return NG_BUSY;
  }
  return NG_OK;
}
void native_worker_execute(NativeGpuWorker* worker,GpuChannel* channel,u64 qmd,u32 action,u32 lease){
  pm_lock(&worker->lock); worker->channel=channel;
  char error[160]; int accepted=swr_native_qmd_signal(worker->qmd,qmd,action,error,sizeof error);
  if(!accepted) WARN("native QMD3 %llx rejected: %s\n",(unsigned long long)qmd,error);
  worker->channel=NULL; pm_unlock(&worker->lock);
  nv_native_range_release(worker->owner,lease); native_worker_unref(worker);
}
