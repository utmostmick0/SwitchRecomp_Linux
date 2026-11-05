/* SDK 22's first mutex lock must see the same ID as the kernel thread object. */
#include "../runtime/rt.h"
#include <assert.h>
u8* g_mem;
GuestFn* g_disp;
u64 g_code_lo,g_code_hi;
const RtAPI g_rtapi={0};
void pm_init(PMutex* p){ pthread_mutex_init(&p->l,NULL); }
void pm_lock(PMutex* p){ pthread_mutex_lock(&p->l); }
void pm_unlock(PMutex* p){ pthread_mutex_unlock(&p->l); }
void pc_init(PCond* c){ pthread_cond_init(&c->c,NULL); }
u64 kmem_alloc(u64 size,u32 state,u32 perm){
  static u64 next=0x1000; assert(size==0x1000 && state==MS_TLS && perm==PERM_RW);
  u64 address=next; next+=size; assert(next<=0x4000); return address;
}
int main(void){
  u8 memory[0x4000]; memset(memory,0xa5,sizeof memory); g_mem=memory; kernel_init();
  KThread* a=kthread_create(0x100,7,0x800,44,0);
  KThread* b=kthread_create(0x200,9,0x900,32,1);
  assert(a && b && a->id && b->id && a->id!=b->id && a->tls!=b->tls);
  assert(gr32(a->tls+0x110)==a->id && gr32(b->tls+0x110)==b->id);
  assert(a->cpu.tpidrro==a->tls && b->cpu.tpidrro==b->tls);
  assert(a->cpu.x[0]==7 && a->cpu.x[31]==0x800 && a->cpu.pc==0x100);
  assert(gr64(a->tls+0x1f8)==0 && gr64(b->tls+0x1f8)==0);
  for(u32 i=0;i<0x1000;i++) if(i<0x110 || i>=0x114){
    assert(memory[a->tls+i]==0 && memory[b->tls+i]==0);
  }
  pthread_cond_destroy(&a->cv.c); pthread_cond_destroy(&b->cv.c);
  puts("SDK 22 thread-local kernel ID checks passed."); return 0;
}
