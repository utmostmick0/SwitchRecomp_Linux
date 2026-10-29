/* Large guest-style static TLS must not prevent small service timer threads. */
#include "../runtime/plat.c"
#include <assert.h>
static __thread volatile u8 guest_tls[128*1024];
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static unsigned finished;
static void worker(void* arg){
  unsigned id=(unsigned)(uintptr_t)arg;
  guest_tls[0]=(u8)id; guest_tls[sizeof guest_tls-1]=(u8)(id+1);
  assert(guest_tls[0]==id && guest_tls[sizeof guest_tls-1]==id+1);
  pthread_mutex_lock(&mutex); finished++; pthread_cond_signal(&condition); pthread_mutex_unlock(&mutex);
}
int main(void){
  guest_tls[0]=99;
  for(unsigned i=1;i<=3;i++) assert(!plat_thread(worker,(void*)(uintptr_t)i,i==3?0:64*1024));
  struct timespec until; assert(!clock_gettime(CLOCK_REALTIME,&until)); until.tv_sec+=5;
  pthread_mutex_lock(&mutex);
  while(finished<3) assert(!pthread_cond_timedwait(&condition,&mutex,&until));
  pthread_mutex_unlock(&mutex); assert(guest_tls[0]==99);
  puts("Small-stack platform threads with 128 KiB static TLS passed."); return 0;
}
