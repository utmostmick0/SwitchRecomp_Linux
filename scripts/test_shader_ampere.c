#include "../runtime/shader.c"
#include <assert.h>

int g_texoff_emul;

static void set_bits(u32 words[4], u32 lo, u32 count, u32 value){
  for(u32 bit=0;bit<count;bit++){
    u32 index=lo+bit;
    if((value>>bit)&1) words[index>>5]|=1u<<(index&31);
  }
}

int main(void){
  const u32 eden_samples[2][4]={
    {0x34007234,0x30000034,0x00702800,0x000fe400},
    {0x28007234,0x30000028,0x00702800,0x000fe400}
  };
  const u64 eden_expected[2]={
    0x5d21001033473407ULL,
    0x5d21001032872807ULL
  };
  for(int i=0;i<2;i++){
    u64 result=0;
    assert(shader_ampere_lower_hsetp2(eden_samples[i],&result));
    assert(result==eden_expected[i]);
  }

  u32 immediate[4]={0};
  set_bits(immediate,0,12,0x434);
  set_bits(immediate,12,4,3);
  set_bits(immediate,24,8,8);
  set_bits(immediate,32,32,0x3f123456);
  set_bits(immediate,69,2,1);
  set_bits(immediate,71,1,1);
  set_bits(immediate,72,1,1);
  set_bits(immediate,73,1,1);
  set_bits(immediate,74,2,2);
  set_bits(immediate,76,4,9);
  set_bits(immediate,80,1,1);
  set_bits(immediate,81,3,4);
  set_bits(immediate,84,3,2);
  set_bits(immediate,87,3,6);
  set_bits(immediate,90,1,1);
  u64 lowered_immediate[2]={0};
  assert(shader_ampere_lower_hsetp2_imm(immediate,lowered_immediate));
  assert(lowered_immediate[0]==(0x0100000000000000ULL | (3ull<<16) | 250 |
                                 (15ull<<12) | (0x3f123456ull<<20)));
  u64 expected_hsetp2=0x5d20000000000000ULL | (3ull<<16) | 2 | (4ull<<3) |
                       (1ull<<6) | (8ull<<8) | (250ull<<20) | (9ull<<35) |
                       (6ull<<39) | (1ull<<42) | (1ull<<43) | (1ull<<44) |
                       (1ull<<45) | (2ull<<47) | (1ull<<49);
  assert(lowered_immediate[1]==expected_hsetp2);
  assert(!shader_ampere_lower_hsetp2_imm(NULL,lowered_immediate));
  assert(!shader_ampere_lower_hsetp2_imm(immediate,NULL));
  immediate[0]=(immediate[0]&~0xfffu)|0x234;
  assert(!shader_ampere_lower_hsetp2_imm(immediate,lowered_immediate));

  u32 words[4]={0};
  set_bits(words,0,12,0x234);
  set_bits(words,9,3,1);
  set_bits(words,12,4,5);
  set_bits(words,24,8,8);
  set_bits(words,32,8,12);
  set_bits(words,60,2,2);
  set_bits(words,62,1,1);
  set_bits(words,63,1,1);
  set_bits(words,69,2,2);
  set_bits(words,71,1,1);
  set_bits(words,72,1,1);
  set_bits(words,73,1,1);
  set_bits(words,74,2,3);
  set_bits(words,76,4,9);
  set_bits(words,80,1,1);
  set_bits(words,81,3,4);
  set_bits(words,84,3,2);
  set_bits(words,87,4,6);

  u64 lowered=0;
  assert(shader_ampere_lower_hsetp2(words,&lowered));
  u64 expected=0x5d20000000000000ULL | ((u64)5<<16) | ((u64)4<<3) | 2 |
               ((u64)1<<6) | ((u64)8<<8) | ((u64)12<<20) |
               ((u64)2<<28) | ((u64)1<<30) | ((u64)1<<31) |
               ((u64)9<<35) | ((u64)6<<39) | ((u64)1<<43) |
               ((u64)1<<44) | ((u64)2<<45) | ((u64)3<<47) | ((u64)1<<49);
  assert(lowered==expected);

  words[0]|=1u<<16;
  assert(!shader_ampere_lower_hsetp2(words,&lowered));
  assert(!shader_ampere_lower_hsetp2(NULL,&lowered));
  assert(!shader_ampere_lower_hsetp2(words,NULL));
  words[0]&=~(1u<<16);
  words[0]=(words[0]&~(7u<<9))|(2u<<9);
  assert(!shader_ampere_lower_hsetp2(words,&lowered));
  puts("Ampere HSETP2 lowering checks passed.");
  return 0;
}
