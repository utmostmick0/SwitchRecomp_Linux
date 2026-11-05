#include "../runtime/cpu_helpers.c"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static u32 counter;

static void* add_repeatedly(void* arg){
  u32 count=(u32)(uintptr_t)arg;
  for(u32 i=0;i<count;i++) swr_lse_op(&counter,4,0,1);
  return NULL;
}

int main(void){
  u64 pair[2] __attribute__((aligned(16)))={0x0123456789abcdefull,0xfedcba9876543210ull};
  u64 old0=0,old1=0;
  assert(swr_cas128_value(pair,pair[0],pair[1],1,2,&old0,&old1));
  assert(old0==0x0123456789abcdefull && old1==0xfedcba9876543210ull);
  assert(pair[0]==1 && pair[1]==2);
  assert(!swr_cas128_value(pair,3,4,5,6,&old0,&old1));
  assert(old0==1 && old1==2 && pair[0]==1 && pair[1]==2);

  u8 b=0x12; assert(swr_cas_value(&b,1,0x12,0x34)==0x12 && b==0x34);
  assert(swr_cas_value(&b,1,0x12,0x56)==0x34 && b==0x34);
  b=0xff; assert(swr_lse_op(&b,1,0,1)==0xff && b==0);
  u16 h=0x1234; assert(swr_cas_value(&h,2,0x1234,0xabcd)==0x1234 && h==0xabcd);
  h=0xffff; assert(swr_lse_op(&h,2,0,1)==0xffff && h==0);
  u32 w=0x12345678; assert(swr_cas_value(&w,4,0,1)==0x12345678 && w==0x12345678);
  u64 d=0x123456789abcdef0ull; assert(swr_cas_value(&d,8,d,0xfedcba9876543210ull)==0x123456789abcdef0ull);
  assert(d==0xfedcba9876543210ull);
  d=~0ull; assert(swr_lse_op(&d,8,0,1)==~0ull && d==0);

  w=0xfffffff0; assert(swr_lse_op(&w,4,0,0x30)==0xfffffff0 && w==0x20);
  w=0xff; assert(swr_lse_op(&w,4,1,0x0f)==0xff && w==0xf0);
  w=0x55; assert(swr_lse_op(&w,4,2,0xff)==0x55 && w==0xaa);
  w=0x50; assert(swr_lse_op(&w,4,3,0x0f)==0x50 && w==0x5f);
  w=0xfffffff0; assert(swr_lse_op(&w,4,4,0x20)==0xfffffff0 && w==0x20);
  w=0x10; assert(swr_lse_op(&w,4,5,0xfffffff0)==0x10 && w==0xfffffff0);
  w=2; assert(swr_lse_op(&w,4,6,3)==2 && w==3);
  w=2; assert(swr_lse_op(&w,4,7,3)==2 && w==2);
  w=2; assert(swr_lse_op(&w,4,8,3)==2 && w==3);

  enum { THREADS=4, ITERATIONS=25000 };
  pthread_t threads[THREADS]; counter=0;
  for(uintptr_t i=0;i<THREADS;i++) assert(!pthread_create(&threads[i],NULL,add_repeatedly,(void*)(uintptr_t)ITERATIONS));
  for(int i=0;i<THREADS;i++) assert(!pthread_join(threads[i],NULL));
  assert(counter==THREADS*ITERATIONS);
  puts("LSE atomic checks passed.");
  return 0;
}
