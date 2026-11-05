/* Exercise imported lowering through the C runtime's actual GLSL translator. */
#include "../runtime/shader.c"
#include "../runtime/ns2_shader.h"
#include <assert.h>
int g_texoff_emul;
static const u32 mov[4]={0x00017802,0x3f800000,0x00000f00,0};
static const u32 exit_op[4]={0x0000794d,0,0x03800000,0};
static void put(u8* p,const u32 words[4]){ memcpy(p,words,16); }
static void glsl(SwrNs2Shader* image,int stage,const char* folder){
  ShaderInfo si={0}; si.cs_local[0]=si.cs_local[1]=si.cs_local[2]=1;
  char* text=shader_to_glsl(image->code,image->size,stage,&si);
  assert(text && !si.unsupported);
  char path[1024]; snprintf(path,sizeof path,"%s/native.%s",folder,stage==ST_VS?"vert":stage==ST_FS?"frag":"comp");
  FILE* f=fopen(path,"wb"); assert(f); assert(fwrite(text,1,strlen(text),f)==strlen(text)); fclose(f); free(text);
}
typedef struct { const u8* bytes; u32 size,calls,largest; } Reader;
static int read_shader(void* context,uint64_t address,void* out,uint32_t size){
  Reader* r=context; r->calls++; if(size>r->largest) r->largest=size;
  if(address>r->size || size>r->size-address) return 0;
  memcpy(out,r->bytes+address,size); return 1;
}
int main(int argc,char** argv){
  assert(argc==2); SwrNs2Shader image;
  u8 graphics[0x80+64]={0}; put(graphics+0x80,mov); put(graphics+0x90,exit_op);
  assert(swr_ns2_shader_lower(graphics,sizeof graphics,0,&image));
  assert(image.consumed==0xa0 && image.size==13*8);
  assert(((u64*)image.code)[10]==0 && ((u64*)image.code)[12]==0xe30000000007000fULL);
  assert(((u64*)image.code)[11]==(0x0100000000000000ULL | (7ull<<16) | 1 | (15ull<<12) | (0x3f800000ull<<20)));
  glsl(&image,ST_VS,argv[1]); glsl(&image,ST_FS,argv[1]); free(image.code);
  assert(swr_ns2_shader_lower(graphics+0x80,32,1,&image));
  assert(image.consumed==32); glsl(&image,ST_CS,argv[1]); free(image.code);

  /* Destination crosses Eden's scheduling slot but not this runtime's. */
  const u32 branch[4]={0x00007947,16,0x03800000,0};
  put(graphics+0x80,branch); put(graphics+0x90,mov); put(graphics+0xa0,mov); put(graphics+0xb0,exit_op);
  assert(swr_ns2_shader_lower(graphics,sizeof graphics,0,&image));
  assert(bra_target(((u64*)image.code)[11],11)==13);
  assert(((u64*)image.code)[14]==0 && ((u64*)image.code)[15]==0xe30000000007000fULL);
  free(image.code);

  /* A predicated backward loop must relocate its signed displacement too. */
  const u32 backward[4]={0x00000947,0xffffffe0,0x0383ffff,0};
  put(graphics+0x80,mov); put(graphics+0x90,backward); put(graphics+0xa0,exit_op);
  assert(swr_ns2_shader_lower(graphics,sizeof graphics,0,&image));
  assert(bra_target(((u64*)image.code)[12],12)==11); free(image.code);
  put(graphics+0x80,branch); put(graphics+0x90,mov); put(graphics+0xa0,mov);

  /* EXIT before a forward branch destination must not truncate capture. */
  put(graphics+0x90,exit_op);
  Reader reader={graphics,sizeof graphics,0,0};
  assert(swr_ns2_shader_capture(0,0,read_shader,&reader,&image));
  assert(image.consumed==sizeof graphics && reader.calls==5 && reader.largest==0x80); free(image.code);
  reader.size=sizeof graphics-1; reader.calls=0;
  assert(!swr_ns2_shader_capture(0,0,read_shader,&reader,&image));
  assert(!image.code && strstr(image.error,"unmapped"));
  assert(!swr_ns2_shader_capture(UINT64_MAX,0,read_shader,&reader,&image));

  u8 invalid[16]={0};
  assert(!swr_ns2_shader_lower(invalid,sizeof invalid,1,&image) && !image.code);
  assert(!swr_ns2_shader_lower(NULL,32,1,&image));
  assert(!swr_ns2_shader_lower(graphics,0x80+15,0,&image));
  assert(!swr_ns2_shader_lower(graphics,sizeof graphics,2,&image));
  assert(!swr_ns2_shader_lower(graphics,sizeof graphics,0,NULL));
  const u32 unknown[4]={0x00007fff,0,0,0}; put(graphics+0x80,unknown);
  assert(!swr_ns2_shader_lower(graphics,sizeof graphics,0,&image) && !image.code && image.error_offset==0x80);
  const u32 indirect[4]={0x00007949,0,0,0}; put(graphics+0x80,indirect);
  assert(!swr_ns2_shader_lower(graphics,sizeof graphics,0,&image) && strstr(image.error,"indirect"));
  puts("NS2 native capture, lowering, relocation and GLSL checks passed."); return 0;
}
