/* Versioned SNDD packet validation and actual destination filtering. */
#include "../runtime/audio.c"
#include <assert.h>
u8* g_mem;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
static void put(u8* p,u32 v){ memcpy(p,&v,4); }
static void putf(u8* p,float v){ memcpy(p,&v,4); }
int main(void){
  for(unsigned rev=11;rev<=15;rev++){
    unsigned stride=rev>=15?0xa8:rev>=12?0x90:0x70;
    unsigned used=rev>=15?0x9c:rev>=12?0x84:0x6c;
    u8 wire[0x20+0xa8]={0}; put(wire,0x48444e53); put(wire+8,1);
    u8* p=wire+0x20; put(p,0x44444e53); putf(p+8,1); put(p+0x68,1); p[used]=1;
    for(unsigned stage=0;rev>=12 && stage<2;stage++){
      u8* f=p+0x6c+stage*(rev>=15?0x18:0xc); f[0]=1;
      if(rev>=15) putf(f+4,.5f); else { i16 gain=8192; memcpy(f+2,&gain,2); }
    }
    SvcObj owner={0}; unsigned size=0x20+stride;
    assert(swr_audio_configure_splitters_revision(&owner,wire,size,1,1,rev));
    AudioDestination* d=&audio_state(&owner)->destinations[0]; assert(d->used && d->mix==1);
    float input[240],output[240]; for(unsigned i=0;i<240;i++) input[i]=1;
    const float* result=splitter_filter(d,input,240,output);
    for(unsigned i=0;i<240;i++) assert(fabsf(result[i]-(rev>=12?.25f:1.f))<1e-6f);
    for(unsigned n=1;n<size;n++) assert(!swr_audio_configure_splitters_revision(&owner,wire,n,1,1,rev));
    assert(d->used && d->mix==1);
    put(p+4,1); assert(!swr_audio_configure_splitters_revision(&owner,wire,size,1,1,rev)); put(p+4,0);
    if(rev>=15){ putf(p+0x70,NAN); assert(!swr_audio_configure_splitters_revision(&owner,wire,size,1,1,rev)); }
    swr_audio_destroy(&owner);
  }
  /* Exact size/count topology of MP4's first REV15 packet. */
  unsigned size=0x20+768*0xa8; assert(size==129056); u8* packet=calloc(1,size); assert(packet);
  put(packet,0x48444e53); put(packet+8,768);
  for(unsigned i=0;i<768;i++){u8* p=packet+0x20+i*0xa8;put(p,0x44444e53);put(p+4,i);}
  SvcObj owner={0}; assert(swr_audio_configure_splitters_revision(&owner,packet,size,128,768,15));
  assert(audio_state(&owner)->ndestinations==768); swr_audio_destroy(&owner); free(packet);
  puts("REV11-15 splitter layout, filtering and bounds checks passed.");return 0;
}
