/* Native audren RequestUpdate layout and bounds checks, without a host audio device. */
#define swr_audio_submit_at test_submit
#include "../runtime/main.c"
#undef swr_audio_submit_at
#include <assert.h>

u8* g_mem;
int g_verbose;
static u64 now,expected_frames; static unsigned submitted,notifications;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void kernel_dump_regs(CPU* cpu){ (void)cpu; assert(0); }
void swr_svc(CPU* cpu,u32 imm){ (void)cpu; (void)imm; assert(0); }
void plat_yield(void){ assert(0); }
u64 plat_ns(void){ return now; }
void plat_sleep_ns(i64 ns){ assert(ns>=0); now+=(u64)ns; }
void pm_lock(PMutex* p){ pthread_mutex_lock(&p->l); }
void pm_unlock(PMutex* p){ pthread_mutex_unlock(&p->l); }
void audren_signal_ready(int ready){ (void)ready; }
void test_submit(const float* samples,unsigned frames,u64 end){ (void)samples; (void)end; assert(frames==240); submitted++; }
void audren_notify_update(SvcObj* owner){
  if(owner->u[3]) return;
  u32 header[16]; memcpy(header,g_mem+12000,64); u64 frames;
  memcpy(&frames,g_mem+12000+header[15]-header[10],8);
  assert(frames==expected_frames && frames==owner->u[5]); notifications++;
}
void mem_query(u64 address,MemBlock* out){
  memset(out,0,sizeof *out); out->size=16384; out->state=MS_NORMAL; out->perm=address<16384?PERM_RW:0;
}
static void put32(u8* p,u32 value){ memcpy(p,&value,4); }
static void mix_record(u8* p,u32 id){
  float one=1; memcpy(p,&one,4); put32(p+8,1); p[12]=1; put32(p+16,id);
  put32(p+0x924,0x7fffffff); put32(p+0x928,0x7fffffff);
}
int main(void){
  u8 memory[16384]={0},parameters[256]={0}; g_mem=memory;
  SvcObj owner={0}; owner.data=parameters; owner.u[3]=1;
  put32(parameters+0x24,1); put32(parameters+0x28,1);
  u32 header[16]={0}; header[0]=0x33564552; header[6]=2*0x930; header[9]=0x20;
  header[15]=0x40+header[6]+header[9]; memcpy(memory+1024,header,64);
  put32(memory+1088,0x48444e53);
  mix_record(memory+1120,0); mix_record(memory+1120+0x930,1);
  IpcCtx c={0}; c.na=1; c.a[0].addr=1024; c.a[0].size=header[15];
  c.nb=1; c.b[0].addr=12000; c.b[0].size=1024;
  assert(audren_update(&owner,&c)==0);
  u32 result; memcpy(&result,memory+12000,4); assert(result==header[0]);
  /* Sparse mix updates retain the two buses created by the first IPC. */
  header[0]=0x37564552; header[6]=0x20; header[15]=0x80;
  memcpy(memory+1024,header,64); memset(memory+1088,0,64);
  put32(memory+1088,0x48444e53); put32(memory+1120,0xcafebabe);
  c.a[0].size=header[15]; assert(audren_update(&owner,&c)==0);
  c.a[0].size=63; assert(audren_update(&owner,&c)==RC(153,4));
  c.a[0].size=header[15]; put32(memory+1024+60,0xffffffff);
  assert(audren_update(&owner,&c)==RC(153,4));
  put32(memory+1024+60,header[15]); put32(memory+1092,1);
  assert(audren_update(&owner,&c)==RC(153,4));
  /* Rendered-frame timeline counts completed quanta, not IPC calls or stopped updates. */
  memset(parameters,0,sizeof parameters); memset(header,0,sizeof header);
  header[0]=0x37564552; header[15]=0x40; memcpy(memory+1024,header,64);
  c.a[0].size=0x40; owner.u[3]=0; owner.u[5]=0; owner.u[6]=0;
  now=1000000000ull; expected_frames=1;
  assert(!audren_update(&owner,&c) && submitted==1 && notifications==1);
  now+=15000000; expected_frames=4;
  assert(!audren_update(&owner,&c) && submitted==4 && notifications==2);
  owner.u[3]=1; assert(!audren_update(&owner,&c));
  assert(owner.u[5]==4 && submitted==4 && notifications==2);
  swr_audio_destroy(&owner); assert(!owner.u[4]);
  puts("Audren update checks passed."); return 0;
}
