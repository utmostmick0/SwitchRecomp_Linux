/* A delayed host sample must not underflow after another thread rebases time. */
#include "../runtime/main.c"
#include <assert.h>
int g_verbose;
void logf_(int level,const char* fmt,...){ (void)level;(void)fmt; }
void fatal(const char* fmt,...){ (void)fmt;abort(); }
void kernel_dump_regs(CPU* cpu){ (void)cpu;assert(0); }
void swr_svc(CPU* cpu,u32 imm){ (void)cpu;(void)imm;assert(0); }
void plat_yield(void){ assert(0); }
static u64 host_now;
static int rebase_on_lock;
u64 plat_ns(void){ return host_now; }
void pm_lock(PMutex* lock){
  (void)lock;
  if(rebase_on_lock){ g_vt_ar=101;g_vt_av=200;host_now=102;rebase_on_lock=0; }
}
void pm_unlock(PMutex* lock){ (void)lock; }
int main(void){
  g_t0=0;g_vt_ar=100;g_vt_av=1000;g_vt_s=.5;
  assert(guest_ns_raw(99)==1000 && guest_ns_raw(100)==1000 && guest_ns_raw(110)==1005);
  /* Force a rebase between the caller's first clock sample and its clock lock. */
  g_vt_mi=1;g_vt_max=0;host_now=100;rebase_on_lock=1;g_vt_s=1;
  assert(guest_ns()==201);
  host_now=103;assert(guest_ns()==202);
  host_now=100;assert(guest_ns()==202); /* monotonic clamp */
  host_now=110;assert(guest_ns()==209); /* a stale sample cannot pin the clock */
  puts("Guest clock rebase race and monotonic deadline checks passed.");return 0;
}
