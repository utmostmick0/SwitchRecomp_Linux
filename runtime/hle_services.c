/* Reusable SDK service hooks adapted from PrimeRecomp runtime/hle/pl.c and
   runtime/services/time.c. Copyright (c) 2026 Peiikai and PrimeRecomp contributors.
   MIT notice: ../LICENSES/primerecomp-MIT.txt. */
#include "rt.h"

typedef struct { u64 address,size; } SharedFont;
static SharedFont fonts[2];
static PMutex font_lock;
static int fonts_loaded;
#define FONT_LIMIT (32u<<20)

static u32 font_be32(const u8* p){ return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static void font_put32(u8* p,u32 v){ p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static int sfnt_valid(const u8* data,size_t size){
  if(size<12 || (font_be32(data)!=0x10000 && memcmp(data,"OTTO",4))) return 0;
  unsigned tables=(unsigned)data[4]<<8 | data[5];
  if(!tables || tables>(size-12)/16) return 0;
  for(unsigned i=0;i<tables;i++){
    const u8* e=data+12+i*16; size_t off=font_be32(e+8),len=font_be32(e+12);
    if(off>size || len>size-off) return 0;
  }
  return 1;
}

static u8* font_collection_face(const u8* data,size_t size,size_t* out_size){
  if(size<16 || memcmp(data,"ttcf",4) || !font_be32(data+8)) return NULL;
  size_t face=font_be32(data+12);
  if(face>size || size-face<12) return NULL;
  unsigned tables=(unsigned)data[face+4]<<8 | data[face+5];
  if(!tables || tables>(size-face-12)/16) return NULL;
  size_t total=12+(size_t)tables*16;
  for(unsigned i=0;i<tables;i++){
    const u8* e=data+face+12+i*16; size_t off=font_be32(e+8),len=font_be32(e+12);
    if(off>size || len>size-off || len>FONT_LIMIT || total>FONT_LIMIT-((len+3)&~(size_t)3)) return NULL;
    total+=(len+3)&~(size_t)3;
  }
  u8* out=calloc(1,total); if(!out) return NULL;
  memcpy(out,data+face,12); size_t pos=12+(size_t)tables*16;
  for(unsigned i=0;i<tables;i++){
    const u8* e=data+face+12+i*16; size_t off=font_be32(e+8),len=font_be32(e+12);
    u8* dst=out+12+i*16; memcpy(dst,e,8); font_put32(dst+8,(u32)pos); font_put32(dst+12,(u32)len);
    memcpy(out+pos,data+off,len); pos+=(len+3)&~(size_t)3;
  }
  *out_size=total; return out;
}

static int font_load(const char* path,SharedFont* font){
  if(!path || !*path) return 0;
  FILE* f=fopen(path,"rb"); if(!f) return 0;
  if(fseek(f,0,SEEK_END)){ fclose(f); return 0; }
  long length=ftell(f);
  if(length<12 || length>FONT_LIMIT || fseek(f,0,SEEK_SET)){ fclose(f); return 0; }
  size_t size=(size_t)length; u8* data=malloc(size);
  if(!data){ fclose(f); return 0; }
  size_t got=fread(data,1,size,f); fclose(f);
  if(got!=size){ free(data); return 0; }
  if(!sfnt_valid(data,size) && memcmp(data,"ttcf",4)){
    /* Console .bfttf wraps an XOR-obfuscated sfnt in an eight-byte header. */
    for(int order=0;order<2;order++){
      u32 key=order?0x06186249u:0x49621806u, encoded; memcpy(&encoded,data+4,4);
      size_t n=encoded^key; if(n<12 || n>size-8) continue;
      u8* decoded=malloc(n); if(!decoded) continue;
      memcpy(decoded,data+8,n);
      for(size_t i=0;i+4<=n;i+=4){ u32 word; memcpy(&word,decoded+i,4); word^=key; memcpy(decoded+i,&word,4); }
      if(sfnt_valid(decoded,n)){ free(data); data=decoded; size=n; break; }
      free(decoded);
    }
  }
  if(!memcmp(data,"ttcf",4)){
    size_t n=0; u8* face=font_collection_face(data,size,&n); free(data); data=face; size=n;
  }
  if(!data || !sfnt_valid(data,size)){ free(data); return 0; }
  u64 address=kmem_alloc((size+0xfff)&~0xfffull,MS_STATIC,PERM_RW);
  if(!address){ free(data); return 0; }
  memcpy(GP(address),data,size); free(data);
  font->address=address; font->size=size;
  LOG("pl: shared font %s (%zu bytes)\n",path,size); return 1;
}

static SharedFont shared_font(u64 type){
  SharedFont result={0}; if(type>5) return result;
  pm_lock(&font_lock);
  if(!fonts_loaded){
    fonts_loaded=1;
    char root[1200]; snprintf(root,sizeof root,"%sfonts/",g_exedir);
    static const char* names[]={"nintendo_udsg-r_std_003.bfttf","nintendo_udsg-r_std_003.ttf",NULL};
    font_load(getenv("SWR_FONT"),&fonts[0]);
    for(unsigned i=0;names[i] && !fonts[0].address;i++){
      char path[1400]; snprintf(path,sizeof path,"%s%s",root,names[i]); font_load(path,&fonts[0]);
    }
    static const char* fallback[]={"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf","/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
      "C:/Windows/Fonts/arial.ttf",NULL};
    for(unsigned i=0;fallback[i] && !fonts[0].address;i++) font_load(fallback[i],&fonts[0]);
    char path[1400]; snprintf(path,sizeof path,"%snintendo_ext_003.bfttf",root);
    if(!font_load(path,&fonts[1])){ snprintf(path,sizeof path,"%snintendo_ext_003.ttf",root); font_load(path,&fonts[1]); }
  }
  result=type==5 && fonts[1].address?fonts[1]:fonts[0];
  pm_unlock(&font_lock); return result;
}

static void font_state(CPU* c,u64 entry){ (void)entry; c->x[0]=shared_font(c->x[0]).address?1:0; c->pc=c->x[30]; }
static void font_address(CPU* c,u64 entry){ (void)entry; c->x[0]=shared_font(c->x[0]).address; c->pc=c->x[30]; }
static void font_size(CPU* c,u64 entry){ (void)entry; c->x[0]=shared_font(c->x[0]).size; c->pc=c->x[30]; }
static void clock_now(CPU* c,u64 entry){
  (void)entry; u64 dest=c->x[0];
  if(dest && dest<AS_SIZE && AS_SIZE-dest>=8){ i64 now=plat_unix_time(); memcpy(GP(dest),&now,8); c->x[0]=0; }
  else c->x[0]=RC(116,1);
  c->pc=c->x[30];
}

/* libc realtime queries may use a different SDK clock callback from time:u.
   Serve supported POSIX clocks with initialized native timespecs; preserve the
   guest implementation for unsupported clocks, invalid memory, or host failure. */
typedef struct { u64 entry; GuestFn original; } ClockHook;
static ClockHook clock_hooks[16];
static unsigned clock_hook_count;
static void clock_gettime_native(CPU* c,u64 entry){
  ClockHook* hook=NULL;
  for(unsigned i=0;i<clock_hook_count;i++) if(clock_hooks[i].entry==entry){ hook=&clock_hooks[i]; break; }
  if(!hook) return;
  u32 kind=(u32)c->x[0]; u64 dest=c->x[1],ns=0; MemBlock block;
  int valid=dest && dest<AS_SIZE && AS_SIZE-dest>=16;
  if(valid){ mem_query(dest,&block); valid=(block.perm&PERM_W) && dest>=block.addr &&
    dest-block.addr<=block.size && 16<=block.size-(dest-block.addr); }
  if(valid && (kind==0 || kind==1)){
    int ready=kind==0?plat_realtime(&ns):(ns=plat_ns(),1);
    if(ready){ i64 ts[2]={(i64)(ns/1000000000ull),(i64)(ns%1000000000ull)};
      memcpy(GP(dest),ts,sizeof ts); c->x[0]=0; c->pc=c->x[30]; return; }
  }
  hook->original(c,entry);
}
GuestFn hle_clock_adapter(const char* name,u64 entry,GuestFn original){
  if(strcmp(name,"clock_gettime") || !original || clock_hook_count>=16) return NULL;
  for(unsigned i=0;i<clock_hook_count;i++) if(clock_hooks[i].entry==entry) return clock_gettime_native;
  clock_hooks[clock_hook_count++]=(ClockHook){entry,original}; return clock_gettime_native;
}

void hle_services_init(void){ pm_init(&font_lock); }
GuestFn hle_service_lookup(const char* name){
  if(!strcmp(name,"_ZN2nn2pl22GetSharedFontLoadStateENS0_14SharedFontTypeE")) return font_state;
  if(!strcmp(name,"_ZN2nn2pl20GetSharedFontAddressENS0_14SharedFontTypeE")) return font_address;
  if(!strcmp(name,"_ZN2nn2pl17GetSharedFontSizeENS0_14SharedFontTypeE")) return font_size;
  if(!strcmp(name,"_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE")) return clock_now;
  return NULL;
}
