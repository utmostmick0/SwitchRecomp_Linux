/* Descriptor-specialized TEX/TLD declarations, coordinates and stable slot IDs. */
#include "../runtime/shader.c"
#include <assert.h>
int g_texoff_emul;
int main(int argc,char** argv){
  for(int packed=0;packed<2;packed++) for(int load=0;load<2;load++) for(int volume=0;volume<2;volume++){
    u64 code[16]={0};
    code[11]=(load?0xdc00000000000000ull:0xc000000000000000ull)|((u64)2<<28)|((u64)15<<31)|((u64)5<<36)|((u64)4<<8)|((u64)10<<20)|((u64)7<<16);
    if(packed){
      code[11]=(load?0xd200000000000000ull:0xd000000000000000ull)|((u64)2<<53)|((u64)5<<36)|((u64)4<<8)|((u64)10<<20)|((u64)7<<16);
    }
    code[12]=code[11]; code[13]=0xe30000000007000full;
    ShaderInfo info={0}; info.tex_3d_mask=volume?1:0;
    char* source=shader_to_glsl((u8*)code,sizeof code,ST_FS,&info); assert(source);
    assert(info.ntex==1 && info.tex[0].can_3d && info.tex[0].instruction_type==1);
    assert(info.tex[0].type==(volume?2:1));
    assert(strstr(source,volume?"uniform sampler3D":"uniform sampler2D"));
    assert(strstr(source,load?(volume?"ivec3(":"ivec2("):(volume?"vec3(":"vec2(")));
    if(argc==2){char path[2048];snprintf(path,sizeof path,"%s/%s_%s_%s.frag",argv[1],packed?"packed":"tuple",load?"tld":"tex",volume?"3d":"2d");FILE*f=fopen(path,"w");assert(f);fputs(source,f);fclose(f);}
    free(source);
  }
  /* Unsupported resource operations must not be selected by the runtime mask. */
  ShaderInfo info={0}; Ctx context={0}; context.si=&info;
  assert(tex_slot2(&context,5,1,0,0,0,0,0,1)==0);
  assert(info.tex[0].can_3d);
  assert(tex_slot2(&context,5,1,0,0,0,0,0,0)==0);
  assert(!info.tex[0].can_3d);
  puts("Shader dimension checks passed.");return 0;
}
