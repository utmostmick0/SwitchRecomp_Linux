/* Session/domain ownership through the actual native HIPC transport and handle table. */
#include "../runtime/rt.h"
#include <assert.h>

u8* g_mem;
int g_verbose;
__thread void* tls_thread;
static int destroyed;
void logf_(int level,const char* fmt,...){ (void)level; (void)fmt; }
void fatal(const char* fmt,...){ (void)fmt; abort(); }
void pm_init(PMutex* p){ pthread_mutex_init(&p->l,NULL); }
void pm_lock(PMutex* p){ pthread_mutex_lock(&p->l); }
void pm_unlock(PMutex* p){ pthread_mutex_unlock(&p->l); }
static void destroy_child(SvcObj* self){ (void)self; destroyed++; }
static int child(SvcObj* self,IpcCtx* c){ (void)self; out32(c,0xcafe); return 0; }
static int factory(SvcObj* self,IpcCtx* c){
  (void)self; SvcObj* object=svcobj_new("IProfile(test)",child,0);
  object->destroy=destroy_child; out_obj(c,object); return 0;
}
static void put(u8* p,u32 value){ memcpy(p,&value,4); }
static u32 get(const u8* p){ u32 value; memcpy(&value,p,4); return value; }
static u8* request(u32 type,u32 command,u32 domain,int close){
  u8* message=g_mem+0x100; memset(message,0,0x200);
  put(message,type); put(message+4,16);
  u8* payload=message+16;
  if(domain){ payload[0]=close?2:1; payload[2]=16; put(payload+4,domain); payload+=16; }
  put(payload,0x49434653); put(payload+8,command); return message;
}
int main(void){
  u8 memory[1024]={0}; g_mem=memory; pm_init(&g_klock);
  KThread thread={0};
  u32 root=session_new(svcobj_new("root",factory,0)); assert(root);
  u8* message=request(4,0,0,0); ipc_handle(&thread,root,0x100);
  assert(!thread.cpu.x[0]); u32 handle=get(message+12); assert(handle && handle!=root);
  assert(h_get_type(handle,KO_SESSION));
  message=request(4,0,0,0); ipc_handle(&thread,handle,0x100);
  assert(get(message+32)==0xcafe); assert(!h_close(handle) && destroyed==1);

  /* Clone a session, close the original, and use the clone. */
  message=request(5,2,0,0); ipc_handle(&thread,root,0x100); u32 clone=get(message+12);
  assert(clone && clone!=root); assert(!h_close(root));
  request(4,0,0,0); ipc_handle(&thread,clone,0x100); handle=get(message+12);
  assert(h_get_type(handle,KO_SESSION)); assert(!h_close(handle) && destroyed==2);

  /* Convert the clone to a domain, then create, call and close a child object. */
  message=request(5,0,0,0); ipc_handle(&thread,clone,0x100); u32 id=get(message+32); assert(id==1);
  message=request(4,0,id,0); ipc_handle(&thread,clone,0x100);
  assert(get(message+16)==1); u32 child_id=get(message+48); assert(child_id==2);
  message=request(4,0,child_id,0); ipc_handle(&thread,clone,0x100); assert(get(message+48)==0xcafe);
  request(4,0,child_id,1); ipc_handle(&thread,clone,0x100); assert(destroyed==3);
  assert(!h_close(clone)); assert(!h_get_type(clone,KO_SESSION));
  puts("IPC session/domain checks passed."); return 0;
}
