/* Exercise the actual platform retry loop and native guest clock adapter. */
#define clock_gettime test_clock_gettime
#define time test_time
#include "../runtime/plat.c"
#undef clock_gettime
#undef time
#include "../runtime/hle_services.c"
#include <assert.h>

u8* g_mem;
static int attempts,failures,error_code=EAGAIN,original_calls;
int test_clock_gettime(clockid_t id,struct timespec* ts){
  (void)id; attempts++;
  if(attempts<=failures){ errno=error_code; return -1; }
  ts->tv_sec=1700000000; ts->tv_nsec=123456789; return 0;
}
time_t test_time(time_t* out){ time_t value=1700000001; if(out)*out=value; return value; }
void mem_query(u64 address,MemBlock* block){
  memset(block,0,sizeof *block); block->addr=32; block->size=64;
  block->state=MS_NORMAL; block->perm=address>=32 && address<96?PERM_RW:0;
}
static void original_clock(CPU* c,u64 entry){ (void)entry; original_calls++; c->x[0]=0xffffffffu; c->pc=c->x[30]; }
int main(void){
  u64 ns=0; attempts=0; failures=2;
  assert(plat_realtime(&ns) && attempts==3 && ns==1700000000123456789ull);
  attempts=0; failures=100; error_code=EINTR;
  assert(plat_realtime(&ns) && attempts==8 && ns==1700000001000000000ull);
  attempts=0; error_code=EAGAIN;
  assert(plat_realtime(&ns) && attempts==8);
  attempts=0; error_code=EINVAL; ns=77;
  assert(!plat_realtime(&ns) && attempts==1 && ns==77);
  u8 memory[128]; memset(memory,0xa5,sizeof memory); g_mem=memory;
  GuestFn hook=hle_clock_adapter("clock_gettime",0x1000,original_clock); assert(hook);
  CPU c={0}; c.x[1]=32; c.x[30]=0x2000; attempts=0; failures=2; error_code=EAGAIN;
  hook(&c,0x1000); i64 ts[2]; memcpy(ts,memory+32,16);
  assert(c.x[0]==0 && c.pc==0x2000 && ts[0]==1700000000 && ts[1]==123456789 && !original_calls);
  for(int i=0;i<128;i++) if(i<32 || i>=48) assert(memory[i]==0xa5);
  c.x[0]=9; hook(&c,0x1000); assert(original_calls==1);
  c.x[0]=0; c.x[1]=90; hook(&c,0x1000); assert(original_calls==2);
  c.x[0]=0; c.x[1]=32; attempts=0; failures=100; error_code=EINVAL;
  hook(&c,0x1000); assert(original_calls==3);
  puts("Clock retry and guest timespec checks passed."); return 0;
}
