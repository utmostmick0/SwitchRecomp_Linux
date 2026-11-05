/* Real VI image and mapped backing owners, with controlled present completion. */
#include "../runtime/mem.c"
#include "../runtime/nvdrv.c"
#include "../runtime/native_vi2.c"
#include <assert.h>
static u8 backing[AS_BASE+0x20000];
int g_verbose;
void (*g_ww_touch)(u64);
static int admit;
static void (*completion)(void*,int);
static void* completion_context;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void pm_init(PMutex* m){ pthread_mutex_init(&m->l,NULL); }
void pm_lock(PMutex* m){ pthread_mutex_lock(&m->l); }
void pm_unlock(PMutex* m){ pthread_mutex_unlock(&m->l); }
u8* plat_vm_reserve(u64 size){ assert(size==AS_SIZE); return backing; }
int plat_vm_commit(u8* p,u64 size){ assert(p>=backing && p+size<=backing+sizeof backing); return 0; }
void plat_vm_decommit(u8* p,u64 size){ (void)p; (void)size; assert(0); }
int gpu_native_queue_rgba(u64 cpu,u32 width,u32 height,u32 pitch,void (*done)(void*,int),void* context){
  if(!admit) return 0;
  assert(!completion && width==4 && height==2 && pitch==32);
  assert(gr32(cpu)==0x01020304 && gr32(cpu+pitch)==0x05060708);
  completion=done; completion_context=context; return 1;
}
static void finish(int ok){
  assert(completion); void (*done)(void*,int)=completion; void* context=completion_context;
  completion=NULL; completion_context=NULL; done(context,ok);
}
int main(void){
  mem_init(); pm_init(&g_nvlock); assert(!mem_map(AS_BASE+0x1000,0x5000,MS_NORMAL,PERM_RW));
  u64 owner=nv_native_owner_open(),gpu=(1ull<<48)|0x10000;
  u32 memory,image; u64 ticket,next;
  assert(nv_native_memory_register(owner,AS_BASE+0x1000,0x3000,&memory)==NG_OK);
  assert(nv_native_map(owner,memory,gpu,0,0x3000)==NG_OK);
  NativeVi2* vi=native_vi2_create(owner); assert(vi && !native_vi2_create(UINT64_MAX));
  assert(nv_native_owner_close(owner)==NG_BUSY);
  assert(native_vi2_register_rgba(vi,gpu,4,2,12,&image)==NG_INVALID);
  assert(native_vi2_register_rgba(vi,gpu,4,2,32,&image)==NG_OK);
  gw32(AS_BASE+0x1000,0x01020304); gw32(AS_BASE+0x1020,0x05060708);
  assert(native_vi2_acquire(vi,image,&ticket)==NG_OK && ticket);
  assert(native_vi2_unregister(vi,image)==NG_BUSY);
  assert(native_vi2_present(vi,image,ticket,gpu+0x1000,4)==NG_BUSY && !completion);
  gw32(AS_BASE+0x2000,4);
  assert(native_vi2_present(vi,image,ticket,gpu+0x1000,4)==NG_BUSY && !completion);
  admit=1;
  assert(native_vi2_present(vi,image,ticket,gpu+0x1000,4)==NG_OK && completion);
  assert(!native_vi2_completed_frames(vi));
  assert(native_vi2_present(vi,image,ticket,0,0)==NG_BUSY);
  assert(native_vi2_cancel(vi,image,ticket)==NG_BUSY);
  assert(native_vi2_acquire(vi,image,&next)==NG_BUSY);
  assert(nv_native_unmap(owner,gpu,0x3000)==NG_BUSY);
  finish(0); assert(!native_vi2_completed_frames(vi));
  assert(native_vi2_present(vi,image,ticket,0,0)==NG_OK); finish(1);
  assert(native_vi2_completed_frames(vi)==1);
  assert(native_vi2_present(vi,image,ticket,0,0)==NG_BUSY);
  assert(native_vi2_acquire(vi,image,&next)==NG_OK && next!=ticket);
  assert(native_vi2_cancel(vi,image,ticket)==NG_NOT_FOUND);
  assert(native_vi2_cancel(vi,image,next)==NG_OK);
  assert(native_vi2_unregister(vi,image)==NG_OK);
  u32 newer;
  assert(native_vi2_register_rgba(vi,gpu,4,2,32,&newer)==NG_OK && newer!=image);
  assert(native_vi2_acquire(vi,image,&ticket)==NG_NOT_FOUND);
  assert(native_vi2_acquire(vi,newer,&ticket)==NG_OK);
  assert(native_vi2_present(vi,newer,ticket,0,0)==NG_OK);
  native_vi2_unref(vi); /* queued frame retains the object and registered backing */
  assert(nv_native_memory_release(owner,memory)==NG_OK);
  assert(nv_native_unmap(owner,gpu,0x3000)==NG_BUSY);
  assert(nv_native_owner_close(owner)==NG_BUSY);
  finish(1);
  assert(nv_native_unmap(owner,gpu,0x3000)==NG_OK);
  assert(nv_native_owner_close(owner)==NG_OK);
  puts("Native VI2 acquire, fence, retry, presentation completion and teardown checks passed."); return 0;
}
