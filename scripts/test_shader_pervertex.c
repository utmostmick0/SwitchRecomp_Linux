/* Exercise actual vertex generation with and without layer writes. */
#include "../runtime/shader.c"
#include <assert.h>
int g_texoff_emul;
int main(int argc,char** argv){
  for(int layer=0;layer<2;layer++) for(int ext=0;ext<2;ext++){
    u64 code[16]={0}; ShaderInfo info={0}; g_vs_layer_ext=ext;
    code[11]=0xeff0000000000000ull | ((u64)(layer?0x64:0x70)<<20) | ((u64)255<<8) | ((u64)7<<16);
    code[12]=0xeff0000000000000ull | ((u64)0x70<<20) | ((u64)255<<8) | ((u64)7<<16);
    code[13]=0xe30000000007000full;
    char* source=shader_to_glsl((u8*)code,sizeof code,ST_VS,&info); assert(source);
    assert(!info.unsupported && info.writes_layer==layer);
    const char* block="out gl_PerVertex { invariant vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[8]; };";
    const char* found=strstr(source,block); assert(found && !strstr(found+strlen(block),block));
    assert(!layer || strstr(source,"invariant gl_Position;"));
    if(argc==2){ char path[2048]; snprintf(path,sizeof path,"%s/vertex_layer%d_ext%d.vert",argv[1],layer,ext);
      FILE* f=fopen(path,"w"); assert(f);
      /* The GL backend injects this viewport uniform before compiling VS source. */
      char* main_start=strstr(source,"void main()"); assert(main_start);
      fwrite(source,1,(size_t)(main_start-source),f);
      fputs("uniform vec4 swr_vp;\n",f); fputs(main_start,f); fclose(f); }
    free(source);
  }
  puts("Vertex built-in block checks passed."); return 0;
}
