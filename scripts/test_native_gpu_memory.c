/* Actual memory and NVMap owners, not a metadata-only native allocation ledger. */
#include "../runtime/mem.c"
#include "../runtime/nvdrv.c"
#include <assert.h>
static u8 backing[AS_BASE+0x20000];
#define CPU_ADDR(n) (AS_BASE+(n))
static unsigned decommits;
int g_verbose;
void (*g_ww_touch)(u64);
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void pm_init(PMutex* m){ pthread_mutex_init(&m->l,NULL); }
void pm_lock(PMutex* m){ pthread_mutex_lock(&m->l); }
void pm_unlock(PMutex* m){ pthread_mutex_unlock(&m->l); }
u8* plat_vm_reserve(u64 size){ assert(size==AS_SIZE); return backing; }
int plat_vm_commit(u8* ptr,u64 size){ assert(ptr>=backing && ptr+size<=backing+sizeof backing); return 0; }
void plat_vm_decommit(u8* ptr,u64 size){ assert(ptr>=backing && ptr+size<=backing+sizeof backing); decommits++; memset(ptr,0,(size_t)size); }

int main(void){
  mem_init(); pm_init(&g_nvlock);
  assert(!mem_map(CPU_ADDR(0x1000),0x8000,MS_NORMAL,PERM_RW));
  u64 owner=nv_native_owner_open(),other=nv_native_owner_open(); assert(owner && other && owner!=other);
  u32 memory=0,lease=0; u64 address=0,size=0;
  assert(nv_native_memory_register(owner,CPU_ADDR(0x1000),0x3000,&memory)==NG_OK);
  assert(nv_native_memory_info(owner,memory,&address,&size)==NG_OK && address==CPU_ADDR(0x1000) && size==0x3000);
  assert(nv_native_memory_info(other,memory,&address,&size)==NG_NOT_FOUND);
  assert(nv_native_owner_close(owner)==NG_BUSY);
  MemBlock block; mem_query(CPU_ADDR(0x1000),&block); assert(block.dev==1);
  assert(mem_unmap(CPU_ADDR(0x1000),0x1000) && !decommits);
  assert(mem_set_state(CPU_ADDR(0x1000),0x1000,MS_NORMAL,PERM_R));
  assert(mem_set_attr(CPU_ADDR(0x1000),0x1000,1,1));
  assert(mem_device_pin(CPU_ADDR(0x1001),0x1000));
  assert(mem_device_pin(CPU_ADDR(0x8000),0x2000)); /* hole: existing portion must not be pinned */
  mem_query(CPU_ADDR(0x8000),&block); assert(!block.dev);

  const u64 low=0x0400000000ull,high=(1ull<<48)|low;
  assert(gmmu_map(low,CPU_ADDR(0x7000),0x1000));
  assert(nv_native_map(owner,memory,high,0,0x3000)==NG_OK);
  assert(gmmu_translate(low+17)==CPU_ADDR(0x7011) && gmmu_translate(high+17)==CPU_ADDR(0x1011));
  assert(!gmmu_translate((1ull<<49)|low));
  assert(nv_native_map(owner,memory,1ull<<49,0,0x1000)==NG_INVALID);
  assert(nv_native_map(other,memory,high+0x10000,0,0x1000)==NG_NOT_FOUND);
  assert(nv_native_map(owner,memory,high+0x1000,0,0x1000)==NG_BUSY);
  assert(nv_native_map(owner,memory,high+0x10000,0x3000,0x1000)==NG_INVALID);
  assert(!gmmu_map(high,CPU_ADDR(0x7000),0x1000)); /* legacy cannot replace native ownership */
  gmmu_unmap(high,0x3000); assert(gmmu_translate(high)==CPU_ADDR(0x1000));
  u32 h=nvmap_from_id(memory); assert(h);
  u8 request[32]={0}; memcpy(request,&h,4);
  assert(ioctl_nvmap(5,request,sizeof request)!=0 && nvh[h].alive);

  assert(nv_native_range_acquire(owner,high+0x800,0x1000,&lease)==NG_OK);
  assert(nv_native_range_release(other,lease)==NG_NOT_FOUND);
  assert(nv_native_unmap(owner,high,0x3000)==NG_BUSY);
  assert(nv_native_memory_release(owner,memory)==NG_OK);
  assert(nv_native_memory_release(owner,memory)==NG_NOT_FOUND); /* cannot steal the map's reference */
  assert(gmmu_translate(high)==CPU_ADDR(0x1000) && mem_unmap(CPU_ADDR(0x1000),0x3000));
  assert(nv_native_range_release(owner,lease)==NG_OK);
  assert(nv_native_range_release(owner,lease)==NG_NOT_FOUND);
  assert(nv_native_unmap(owner,high,0x3000)==NG_OK);
  assert(!gmmu_translate(high) && gmmu_translate(low)==CPU_ADDR(0x7000));
  mem_query(CPU_ADDR(0x1000),&block); assert(!block.dev);
  assert(!mem_unmap(CPU_ADDR(0x1000),0x3000) && decommits==1);
  assert(nv_native_owner_close(owner)==NG_OK && nv_native_owner_close(other)==NG_OK);
  assert(nv_native_memory_register(owner,CPU_ADDR(0x4000),0x1000,&memory)==NG_NOT_FOUND);
  assert(mem_unmap(CPU_ADDR(0x4000),UINT64_MAX));
  assert(!(gmmu_generation()&1));
  puts("Native GPU ownership, physical pins, leases and 49-bit isolation checks passed."); return 0;
}
