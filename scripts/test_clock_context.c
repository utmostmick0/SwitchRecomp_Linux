/* Native IPC regression checks for service behavior adapted from PrimeRecomp.
   Build with function/data sections and --gc-sections to test these dispatchers
   without starting the frontend, audio devices, or guest threads. */
#include "../runtime/svc_core.c"
#include <assert.h>

u8* g_mem;
int g_opt_lang=1;
void logf_(int lvl,const char* fmt,...){ (void)lvl; (void)fmt; }
u64 plat_ns(void){ return 123000000000ull; }
i64 plat_unix_time(void){ return 1700000000; }
int audren_update(SvcObj* object,IpcCtx* context){ (void)object; (void)context; assert(0); return 0; }
u32 h_new(KObj* object){ (void)object; assert(0); return 0; }
KEvent* kevent_new(const char* name,int autoclear){ (void)name; (void)autoclear; assert(0); return NULL; }
void kevent_signal(KEvent* event){ (void)event; assert(0); }

int main(void){
  u8 bytes[48]; memset(bytes,0xa5,sizeof bytes);
  system_clock_context(bytes+8);
  i64 offset,steady; memcpy(&offset,bytes+8,8); memcpy(&steady,bytes+16,8);
  assert(steady==123 && offset+steady==1700000000);
  assert(!memcmp(bytes+24,g_clock_id,16));
  for(int i=0;i<8;i++) assert(bytes[i]==0xa5 && bytes[40+i]==0xa5);
  IpcCtx c={0}; c.cmd=2;
  assert(!sysclock_disp(NULL,&c) && c.out_size==32);
  assert(!memcmp(c.out,bytes+8,32));
  puts("Clock context getter layout and bounds checks passed."); return 0;
}
