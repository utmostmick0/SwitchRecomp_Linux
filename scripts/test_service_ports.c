/* Native IPC regression checks for service behavior adapted from PrimeRecomp.
   Build with function/data sections and --gc-sections to test these dispatchers
   without starting the frontend, audio devices, or guest threads. */
#include "../runtime/svc_core.c"
#include <assert.h>

u8* g_mem;
int g_opt_lang=1;
volatile int g_quit;
void logf_(int lvl,const char* fmt,...){ (void)lvl; (void)fmt; }
u64 plat_ns(void){ return 123000000000ull; }
i64 plat_unix_time(void){ return 1700000000; }
int audren_update(SvcObj* object,IpcCtx* context){ (void)object; (void)context; assert(0); return 0; }
u32 h_new(KObj* object){ (void)object; assert(0); return 0; }
KEvent* kevent_new(const char* name,int autoclear){ (void)name; (void)autoclear; assert(0); return NULL; }
void kevent_signal(KEvent* event){ (void)event; assert(0); }
/* Manager discovery must not start playback or access guest PCM buffers. */
void pm_lock(PMutex* mutex){ (void)mutex; assert(0); }
void pm_unlock(PMutex* mutex){ (void)mutex; assert(0); }
void mem_query(u64 address,MemBlock* block){ (void)address; (void)block; assert(0); }
void plat_sleep_ns(i64 ns){ (void)ns; assert(0); }
u64 guest_ns(void){ assert(0); return 0; }

int main(void){
  u8 memory[256], raw[32]={0}; g_mem=memory;
  IpcCtx c={0}; c.raw=raw; c.raw_size=16; c.cmd=1;
  memcpy(raw,g_user_id,16);
  assert(acc_disp(NULL,&c)==0 && c.out_size==1 && c.out[0]==1);
  raw[0]^=1; c.out_size=0; acc_disp(NULL,&c); assert(!c.out[0]);
  c.raw_size=8; c.out_size=0; acc_disp(NULL,&c); assert(!c.out[0]);
  c.raw=NULL; c.raw_size=16; c.out_size=0; acc_disp(NULL,&c); assert(!c.out[0]);

  /* Exercise both auto-select descriptor forms, including short buffers. */
  for(int kind=0;kind<2;kind++) for(int size=0;size<=32;size++){
    memset(memory,0xa5,sizeof memory); memset(&c,0,sizeof c); c.cmd=141;
    if(kind){ c.nc=1; c.c[0].addr=32; c.c[0].size=size; }
    else { c.nb=1; c.b[0].addr=32; c.b[0].size=size; }
    acc_disp(NULL,&c);
    for(int i=0;i<32;i++) assert(memory[i]==0xa5);
    for(int i=32+size;i<256;i++) assert(memory[i]==0xa5);
    for(int i=0;i<size;i++) assert(memory[32+i]==(size>=16 && i<16? ((u8*)g_user_id)[i] : 0));
  }

  memset(&c,0,sizeof c); c.cmd=5; acc_disp(NULL,&c);
  assert(c.nout_objs==1 && !strcmp(c.out_objs[0]->name,"IProfile"));
  SvcObj* profile=c.out_objs[0]; memset(&c,0,sizeof c); c.cmd=1;
  profile->dispatch(profile,&c);
  assert(c.out_size==0x38 && !memcmp(c.out,g_user_id,16)); svcobj_unref(profile);
  memset(&c,0,sizeof c); c.cmd=101; acc_disp(NULL,&c);
  assert(c.nout_objs==1 && !strcmp(c.out_objs[0]->name,"IManagerForApplication"));
  svcobj_unref(c.out_objs[0]);

  u8 ctx[32]; i64 offset,steady; system_clock_context(ctx);
  memcpy(&offset,ctx,8); memcpy(&steady,ctx+8,8);
  assert(steady==123 && offset+steady==1700000000 && !memcmp(ctx+16,g_clock_id,16));
  memset(&c,0,sizeof c); c.cmd=2; assert(!sysclock_disp(NULL,&c));
  assert(c.out_size==sizeof ctx && !memcmp(c.out,ctx,sizeof ctx));
  g_opt_lang=17; memset(&c,0,sizeof c); set_disp(NULL,&c);
  assert(c.out_size==8 && !memcmp(c.out,"pt-BR",5));
  g_opt_lang=-1; memset(&c,0,sizeof c); set_disp(NULL,&c);
  assert(!memcmp(c.out,"en-US",5));
  for(int size=0;size<32;size++){
    memset(memory,0xa5,sizeof memory); memset(&c,0,sizeof c); c.cmd=5;
    c.nb=1; c.b[0].addr=32; c.b[0].size=size; set_disp(NULL,&c);
    u32 n; memcpy(&n,c.out,4); assert(n==(u32)size/8);
    for(int i=32+(size/8)*8;i<256;i++) assert(memory[i]==0xa5);
  }
  memset(&c,0,sizeof c); c.cmd=5; c.nb=1; c.b[0].size=1ull<<40; set_disp(NULL,&c);
  u32 n; memcpy(&n,c.out,4); assert(!n);
  memset(memory,0xa5,sizeof memory); memset(&c,0,sizeof c); c.cmd=11;
  c.nb=1; c.b[0].addr=32; c.b[0].size=3; set_disp(NULL,&c);
  assert(!memcmp(memory+32,"Swi",3) && memory[35]==0xa5);
  u8 params[0x34]={0}; memset(&c,0,sizeof c); c.raw=params; c.raw_size=sizeof params;
  assert(!audren_disp(NULL,&c) && c.nout_objs==1);
  SvcObj* renderer=c.out_objs[0]; memset(&c,0,sizeof c); c.cmd=9;
  assert(!renderer->dispatch(renderer,&c)); memcpy(&n,c.out,4); assert(n==100);
  u32 limit=37; memset(&c,0,sizeof c); c.cmd=8; c.raw=(u8*)&limit; c.raw_size=4;
  renderer->u[5]=123; /* RequestUpdate's frame counter is independent of CPU limit. */
  assert(!renderer->dispatch(renderer,&c)); memset(&c,0,sizeof c); c.cmd=9;
  assert(!renderer->dispatch(renderer,&c)); memcpy(&n,c.out,4); assert(n==37);
  assert(renderer->u[5]==123);
  limit=101; memset(&c,0,sizeof c); c.cmd=8; c.raw=(u8*)&limit; c.raw_size=4;
  assert(renderer->dispatch(renderer,&c)==RC(153,4));
  memset(&c,0,sizeof c); c.cmd=9; assert(!renderer->dispatch(renderer,&c));
  memcpy(&n,c.out,4); assert(n==37 && renderer->u[5]==123);
  /* Start/stop dispatch through the returned renderer, not a fabricated token. */
  memset(&c,0,sizeof c); c.cmd=5; assert(!renderer->dispatch(renderer,&c));
  memset(&c,0,sizeof c); c.cmd=3; assert(!renderer->dispatch(renderer,&c));
  memcpy(&n,c.out,4); assert(n==0);
  memset(&c,0,sizeof c); c.cmd=6; assert(!renderer->dispatch(renderer,&c));
  memset(&c,0,sizeof c); c.cmd=3; assert(!renderer->dispatch(renderer,&c));
  memcpy(&n,c.out,4); assert(n==1);
  svcobj_unref(renderer);
  /* SMO's REV3 capacities stay in the renderer config, not unrelated IPC fields. */
  memset(params,0,sizeof params);
  u32 smo_params[]={48000,240,42,3,96,1,14}; memcpy(params,smo_params,sizeof smo_params);
  memset(&c,0,sizeof c); c.raw=params; c.raw_size=sizeof params;
  assert(!audren_disp(NULL,&c) && c.nout_objs==1); renderer=c.out_objs[0];
  assert(!memcmp(renderer->data,smo_params,sizeof smo_params));
  for(u32 command=0;command<3;command++){
    memset(&c,0,sizeof c); c.cmd=command; assert(!renderer->dispatch(renderer,&c));
    memcpy(&n,c.out,4); assert(c.out_size==4 && n==smo_params[command]);
  }
  svcobj_unref(renderer);
  /* Total renderer buffers span buses; 24 is only the per-bus channel limit. */
  for(u32 buffers=25;buffers<=1536;buffers+=1511){
    memset(params,0,sizeof params); memcpy(params+8,&buffers,4);
    memset(&c,0,sizeof c); c.raw=params; c.raw_size=sizeof params;
    assert(!audren_disp(NULL,&c) && c.nout_objs==1); svcobj_unref(c.out_objs[0]);
  }
  n=1537; memcpy(params+8,&n,4); memset(&c,0,sizeof c); c.raw=params; c.raw_size=sizeof params;
  assert(audren_disp(NULL,&c)==RC(153,4) && !c.nout_objs);
  /* NS2 aud:u returns real independent managers, usable through dispatch. */
  u32 client_arg=0;
  SvcObj* user=new_ns2_auduser(); assert(user);
  for(u32 command=1;command<=2;command++){
    memset(&c,0,sizeof c); c.cmd=command; c.raw=(u8*)&client_arg; c.raw_size=4;
    assert(!user->dispatch(user,&c) && c.nout_objs==1);
    SvcObj* manager=c.out_objs[0];
    assert(manager->dispatch==(command==1?audout_disp:audren_disp));
    memset(&c,0,sizeof c);
    if(command==1){ assert(!manager->dispatch(manager,&c)); memcpy(&n,c.out,4); assert(n==1); }
    else { c.cmd=1; memset(params,0,sizeof params); c.raw=params; c.raw_size=sizeof params;
      assert(!manager->dispatch(manager,&c) && c.out_size==8);
      u64 work_size; memcpy(&work_size,c.out,8); assert(work_size>0); }
    svcobj_unref(manager);
  }
  memset(&c,0,sizeof c); c.cmd=2;
  assert(user->dispatch(user,&c)==RC(153,4) && !c.nout_objs);
  memset(&c,0,sizeof c); c.cmd=999;
  assert(user->dispatch(user,&c)==0xf601 && !c.nout_objs);
  svcobj_unref(user);
  memset(params,0,sizeof params);
  n=4097; memcpy(params+0x18,&n,4); memset(&c,0,sizeof c); c.raw=params; c.raw_size=sizeof params;
  assert(audren_disp(NULL,&c)==RC(153,4) && !c.nout_objs);
  c.cmd=1; assert(audren_disp(NULL,&c)==RC(153,4));
  memset(&c,0,sizeof c); c.raw=params; c.raw_size=0x33;
  assert(audren_disp(NULL,&c)==RC(153,4) && !c.nout_objs);
  puts("Service port checks passed.");
  return 0;
}
