#include "../runtime/shader.c"
#include <assert.h>

int g_texoff_emul;

int main(void){
  u64 code[19]={0};
  code[11]=0xe26000000180000fULL; /* CAL from word 11 to word 15 */
  code[12]=0xe30000000007000fULL; /* main exits after the call */
  code[13]=0x50b000000007000fULL;
  code[15]=0xe32000000007000fULL; /* subroutine RET */
  code[16]=0xe30000000007000fULL; /* terminal EXIT keeps the callee in the hash */
  code[17]=0x50b000000007000fULL;

  u32 len=0;
  shader_hash((const u8*)code,sizeof code,&len);
  assert(len>=16*sizeof(u64));

  ShaderInfo info={0};
  char* source=shader_to_glsl((const u8*)code,sizeof code,ST_VS,&info);
  assert(source && info.unsupported==0);
  assert(strstr(source,"uint call_stack[32]; int call_sp=0;"));
  assert(strstr(source,"call_stack[call_sp++] = 12u;"));
  assert(strstr(source,"pc = 15u; break;"));
  assert(strstr(source,"pc = call_stack[--call_sp]; break;"));

  free(source);
  puts("Shader call/return checks passed.");
  return 0;
}
