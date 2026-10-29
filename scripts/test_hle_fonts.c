/* Native font container validation checks; no proprietary font data required. */
#include "../runtime/hle_services.c"
#include <assert.h>

u8* g_mem;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
u64 kmem_alloc(u64 size,u32 state,u32 perm){
  assert(size<=4096 && state==MS_STATIC && perm==PERM_RW); return 1024;
}

int main(void){
  u8 memory[8192]={0}; g_mem=memory;
  u8 font[32]={0}; font_put32(font,0x10000); font[5]=1;
  memcpy(font+12,"test",4); font_put32(font+20,28); font_put32(font+24,4);
  memcpy(font+28,"data",4);
  assert(sfnt_valid(font,sizeof font));
  for(size_t n=0;n<sizeof font;n++) assert(!sfnt_valid(font,n));
  u8 collection[52]={0}; memcpy(collection,"ttcf",4);
  font_put32(collection+4,0x10000); font_put32(collection+8,1); font_put32(collection+12,20);
  memcpy(collection+20,font,28); font_put32(collection+40,48); memcpy(collection+48,"data",4);
  size_t size=0; u8* face=font_collection_face(collection,sizeof collection,&size);
  assert(face && size==32 && sfnt_valid(face,size) && !memcmp(face+28,"data",4)); free(face);
  for(size_t n=0;n<sizeof collection;n++){ face=font_collection_face(collection,n,&size); assert(!face); }
  font_put32(collection+40,0xfffffff0); assert(!font_collection_face(collection,sizeof collection,&size));

  char path[]="/tmp/swr-font-test-XXXXXX"; int fd=mkstemp(path); assert(fd>=0); close(fd);
  SharedFont out={0};
  for(int format=0;format<3;format++){
    FILE* f=fopen(path,"wb"); assert(f);
    if(!format) assert(fwrite(font,1,sizeof font,f)==sizeof font);
    else {
      u32 key=format==1?0x49621806:0x06186249, header[2]={0,(u32)sizeof font^key};
      assert(fwrite(header,1,8,f)==8);
      for(unsigned i=0;i<sizeof font;i+=4){ u32 word; memcpy(&word,font+i,4); word^=key; assert(fwrite(&word,1,4,f)==4); }
    }
    fclose(f); assert(font_load(path,&out));
    assert(out.size==32 && out.address==1024 && !memcmp(GP(out.address),font,32));
  }
  unlink(path); puts("HLE font checks passed."); return 0;
}
