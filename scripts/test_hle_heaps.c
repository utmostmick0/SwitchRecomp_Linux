/* Original guest call/return and guest unit-buffer lifetime regression checks. */
#include "../runtime/hle_heaps.c"
#include <assert.h>

u8* g_mem;
u64 g_kern_base=0x1000,g_kern_end=0x20000;
int g_verbose;
GuestFn swr_interp_entry;
static int maps,unmaps,steps,fail_map;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void pm_init(PMutex* p){ pthread_mutex_init(&p->l,NULL); }
void pm_lock(PMutex* p){ pthread_mutex_lock(&p->l); }
void pm_unlock(PMutex* p){ pthread_mutex_unlock(&p->l); }
u64 mem_find_free(u64 lo,u64 hi,u64 size,u64 align){ assert(lo==KERN_BASE && hi==KERN_END && size==0x2000 && align==0x1000); return 0x1000; }
int mem_map(u64 address,u64 size,u32 state,u32 perm){ assert(address==0x1000 && size==0x1000 && state==MS_NORMAL && perm==PERM_RW); maps++; return fail_map?-1:0; }
int mem_unmap(u64 address,u64 size){ assert(address==0x1000 && size==0x1000); unmaps++; return 0; }
static void finish(CPU* c,u64 entry){ (void)entry; steps++; c->x[0]=0x1234; c->pc=c->x[30]; }
static void create(CPU* c,u64 entry){ (void)entry; assert(c->x[0]==0x1000 && c->x[1]==4096); c->pc=0x104; }
static void destroy(CPU* c,u64 entry){ (void)entry; assert(c->x[0]==0x1234); c->pc=c->x[30]; }
static void fallback(CPU* c,u64 entry){ (void)entry; assert(c->x[0]==0x200 && c->x[1]==1024); c->x[0]=0; c->pc=c->x[30]; }
static void allocation(CPU* c,u64 entry){ (void)entry; c->x[0]=0; c->pc=c->x[30]; }

int main(void){
  u8 memory[0x20000]={0}; g_mem=memory;
  setenv("SWR_UNIT_HEAP_SCALE","4",1); hle_heaps_init();
  GuestFn dispatch[4]={0}; dispatch[1]=finish;
  CPU c={0}; c.disp=dispatch; c.code_lo=0x100; c.code_hi=0x110;
  GuestFn fn=hle_heap_adapter(heap_symbols[0].name,0x100,create); assert(fn);
  c.x[0]=0x200; c.x[1]=1024; c.x[2]=32; c.x[30]=0x900; fn(&c,0x100);
  assert(c.pc==0x900 && c.x[30]==0x900 && c.x[0]==0x1234 && steps==1 && maps==1);
  fn=hle_heap_adapter(heap_symbols[1].name,0x108,destroy); assert(fn); fn(&c,0x108); assert(unmaps==1);
  /* A host mapping failure falls back to the original guest arguments. */
  fail_map=1; fn=hle_heap_adapter(heap_symbols[0].name,0x10c,fallback); assert(fn);
  c.x[0]=0x200; c.x[1]=1024; fn(&c,0x10c); assert(unmaps==1);
  for(unsigned i=0;i<256;i++) assert(!unit_buffers[i].occupied);
  fn=hle_heap_adapter(heap_symbols[2].name,0x200,allocation); assert(fn);
  c.x[0]=0x1234; fn(&c,0x200); c.x[0]=0x1234; fn(&c,0x200); assert(nfailures==1);
  assert(!hle_heap_adapter("unknown",0x204,allocation));
  puts("Heap adapter checks passed."); return 0;
}
