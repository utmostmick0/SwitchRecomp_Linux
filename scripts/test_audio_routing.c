/* Actual native mixer regression checks, with a small synthetic guest mapping. */
#include "../runtime/audio.c"
#include <assert.h>

u8* g_mem;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void mem_query(u64 address,MemBlock* out){
  memset(out,0,sizeof *out); out->addr=0; out->size=16384; out->state=MS_NORMAL;
  out->perm=address<16384?PERM_RW:0;
}
static void p32(u8* p,u32 v){ memcpy(p,&v,4); }
static void p64(u8* p,u64 v){ memcpy(p,&v,8); }
static void pf(u8* p,float v){ memcpy(p,&v,4); }
static void mix_record(u8* p,u32 id,u32 destination,u32 splitter){
  memset(p,0,0x930); pf(p,1); p32(p+8,1); p[12]=1; p32(p+16,id);
  pf(p+0x24,1); p32(p+0x924,destination); p32(p+0x928,splitter);
}
static void packet(u8* p,u32 mix){
  memset(p,0,0xb0); p32(p,0x48444e53); p32(p+4,1); p32(p+8,1);
  p32(p+0x20,0x49444e53); p32(p+0x28,48000); p32(p+0x2c,1);
  p32(p+0x40,0x44444e53); pf(p+0x48,1); p32(p+0xa8,mix); p[0xac]=1;
}

int main(void){
  u8 memory[16384]={0},voice[0x170]={0},resources[0x70]={0},mixes[2*0x930],status[16],wire[0xb0];
  float output[NF*2]; g_mem=memory;
  for(unsigned i=0;i<2000;i++) pf(memory+4096+i*4,.25f);
  voice[8]=voice[9]=1; voice[11]=5; p32(voice+12,48000); p32(voice+24,1); pf(voice+28,1); pf(voice+32,1);
  p32(voice+0x58,0x7fffffff); p64(voice+0x60,4096); p64(voice+0x68,8000); p32(voice+0x74,2000);
  resources[0x64]=1; pf(resources+4,1);
  mix_record(mixes,0,0x7fffffff,0x7fffffff); mix_record(mixes+0x930,1,0,0x7fffffff);
  SvcObj owner={0}; packet(wire,1);
  assert(swr_audio_configure_splitters(&owner,wire,sizeof wire,1,1));
  AudioState* a=audio_state(&owner);
  for(u32 n=1;n<sizeof wire;n++) assert(!swr_audio_configure_splitters(&owner,wire,n,1,1));
  p32(wire+0x2c,0xffffffff); assert(!swr_audio_configure_splitters(&owner,wire,sizeof wire,1,1));
  assert(a->splitters[0].count==1 && a->destinations[0].mix==1); packet(wire,1);
  swr_audio_begin_update(&owner,1);
  assert(swr_audio_mix(&owner,voice,1,sizeof voice,resources,sizeof resources,mixes,sizeof mixes,NULL,0,status,output)==1);
  for(unsigned i=0;i<NF*2;i++) assert(fabsf(output[i]-.25f)<1e-6f);

  /* No mix changes in a sparse update: cached buses must remain active. */
  u8 sparse[0x20]={0}; p32(sparse,0xcafebabe); voice[8]=0; pf(voice+32,.5f);
  swr_audio_begin_update(&owner,1);
  assert(swr_audio_mix(&owner,voice,1,sizeof voice,resources,sizeof resources,sparse,sizeof sparse,NULL,0,status,output)==1);
  assert(fabsf(output[0]-.25f)<1e-6f);
  assert(output[(NF-1)*2]>.125f && output[(NF-1)*2]<.126f);
  voice[9]=0;
  assert(swr_audio_mix(&owner,voice,1,sizeof voice,resources,sizeof resources,sparse,sizeof sparse,NULL,0,status,output)==0);
  assert(output[0]>.1f && output[(NF-1)*2]<output[0]*.001f);
  swr_audio_destroy(&owner); assert(!owner.u[4]);

  /* Submix splitter edges are processed before the final mix. */
  memset(&owner,0,sizeof owner); voice[8]=voice[9]=1; pf(voice+32,1); p32(voice+0x58,1);
  mix_record(mixes+0x930,1,0x7fffffff,0); packet(wire,0);
  assert(swr_audio_configure_splitters(&owner,wire,sizeof wire,1,1));
  swr_audio_begin_update(&owner,1);
  assert(swr_audio_mix(&owner,voice,1,sizeof voice,resources,sizeof resources,mixes,sizeof mixes,NULL,0,status,output)==1);
  for(unsigned i=0;i<NF*2;i++) assert(fabsf(output[i]-.25f)<1e-6f);
  assert(swr_audio_configure_splitters(&owner,NULL,0,0,0));
  swr_audio_destroy(&owner);
  puts("Audio routing checks passed."); return 0;
}
