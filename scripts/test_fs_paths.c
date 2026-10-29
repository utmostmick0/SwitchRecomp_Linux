/* Descriptor-length and mount traversal checks for the retained native filesystem. */
#include "../runtime/svc_fs.c"
#include <assert.h>
u8* g_mem;
char g_exedir[1024],g_exename[256],g_datadir[1024];
SvcObj* svc_generic(const char* name){ (void)name; assert(0); return NULL; }
static FileData shared_file;
static const char* archive_path;
static RFile archive_file;
static void* read_worker(void* arg){
  unsigned id=(unsigned)(uintptr_t)arg; u64 offset=(6ull<<30)+id*4096;
  for(int iteration=0;iteration<100;iteration++){
    u64 raw[3]={0,offset,64}; IpcCtx c={0}; c.raw=(u8*)raw; c.raw_size=sizeof raw;
    c.nb=1; c.b[0].addr=256+id*64; c.b[0].size=64;
    SvcObj object={0}; object.data=&shared_file;
    assert(!file_disp(&object,&c)); u64 count; memcpy(&count,c.out,8); assert(count==64);
    for(unsigned i=0;i<64;i++) assert(g_mem[256+id*64+i]==(u8)(id+1));
    assert(!rf_read(archive_path,g_mem+256+id*64,64,offset));
    assert(!romfs_read(archive_file.off+offset,g_mem+256+id*64,64));
    for(unsigned i=0;i<64;i++) assert(g_mem[256+id*64+i]==(u8)(id+1));
  }
  return NULL;
}
int main(void){
  u8 memory[2048]={0}; g_mem=memory; memcpy(memory+16,"/abcSECRET",10);
  IpcCtx c={0}; c.na=1; c.a[0].addr=16; c.a[0].size=4;
  char path[32]; read_path(&c,0,path,sizeof path); assert(!strcmp(path,"/abc"));
  c.a[0].size=0; read_path(&c,0,path,sizeof path); assert(!path[0]);
  c.na=0; c.nx=1; c.x[0].addr=16; c.x[0].size=4;
  read_path(&c,0,path,sizeof path); assert(!strcmp(path,"/abc"));
  assert(path_escapes("/../secret") && path_escapes("C:/secret") && path_escapes("/a/..\\secret"));
  assert(!path_escapes("/safe/file.bin"));
  char archive[]="/tmp/swr-archive-test-XXXXXX"; int descriptor=mkstemp(archive); assert(descriptor>=0);
  FILE* file=fdopen(descriptor,"wb+"); assert(file); shared_file.f=file; archive_path=archive; pm_init(&g_romlock);
  for(unsigned id=0;id<7;id++){
    u8 block[64]; memset(block,(int)id+1,sizeof block);
    assert(pwrite(fileno(file),block,sizeof block,(off_t)((6ull<<30)+id*4096))==64);
  }
  archive_file.path=archive; archive_file.off=4096; archive_file.size=(6ull<<30)+6*4096+64; archive_file.logged=1;
  rfiles=&archive_file; nrfiles=1; rtotal=archive_file.off+archive_file.size;
  assert(!fseek(file,19,SEEK_SET)); pthread_t workers[7];
  for(unsigned id=0;id<7;id++) assert(!pthread_create(&workers[id],NULL,read_worker,(void*)(uintptr_t)id));
  for(unsigned id=0;id<7;id++) assert(!pthread_join(workers[id],NULL));
  assert(ftell(file)==19);
  assert(archive_file.stream && rf_pinned_streams==1);
  /* Writes without the explicit flush option remain visible to positional reads. */
  u64 raw[3]={0,128,4}; memcpy(memory+128,"test",4); IpcCtx io={0};
  io.cmd=1; io.raw=(u8*)raw; io.raw_size=sizeof raw; io.na=1; io.a[0].addr=128; io.a[0].size=4;
  SvcObj object={0}; object.data=&shared_file; assert(!file_disp(&object,&io));
  memset(&io,0,sizeof io); io.raw=(u8*)raw; io.raw_size=sizeof raw; io.nb=1; io.b[0].addr=132; io.b[0].size=4;
  assert(!file_disp(&object,&io) && !memcmp(memory+132,"test",4));
  u8 exact[65];
  assert(fs_read_exact(NULL,exact,1,0)<0);
  assert(fs_read_exact(file,NULL,1,0)<0);
  assert(!fs_read_exact(file,exact,64,(6ull<<30)+6*4096));
  assert(fs_read_exact(file,exact,65,(6ull<<30)+6*4096)<0);
  assert(fs_read_exact(file,exact,1,~0ull)<0);
  rraw=file; rtotal=(6ull<<30)+6*4096+65;
  /* Indexed RomFS open returns an actual IStorage, not an empty success. */
  for(int index=0;index<2;index++){
    u8 raw_index=(u8)index; IpcCtx open={0}; open.cmd=205; open.raw=&raw_index; open.raw_size=1;
    int rc=fsp_disp(NULL,&open);
    assert(index?(rc==FS_NOTFOUND && !open.nout_objs):(!rc && open.nout_objs==1));
    if(!index){ SvcObj* storage=open.out_objs[0]; IpcCtx size={0}; size.cmd=4;
      assert(!storage->dispatch(storage,&size)); u64 bytes; memcpy(&bytes,size.out,8); assert(bytes==rtotal);
      svcobj_unref(storage); }
  }
  IpcCtx missing={0}; missing.cmd=205; assert(fsp_disp(NULL,&missing)==FS_NOTFOUND && !missing.nout_objs);
  u64 request[2]={(6ull<<30)+6*4096,65}; memset(&io,0,sizeof io);
  io.raw=(u8*)request; io.raw_size=sizeof request; io.nb=1; io.b[0].addr=1024; io.b[0].size=65;
  assert(romstorage(NULL,&io)==FS_NOTFOUND); rraw=NULL;
  fs_negative_add("/missing",fs_path_epoch,1000);
  assert(fs_negative_hit("/missing",1001)); assert(!fs_negative_hit("/different",1001));
  assert(!fs_negative_hit("/missing",250001000));
  fs_negative_add("/missing",fs_path_epoch,1000); __atomic_add_fetch(&fs_path_epoch,1,__ATOMIC_RELEASE);
  assert(!fs_negative_hit("/missing",1001));
  for(unsigned i=0;i<NOPEN;i++) if(openf[i].f){ assert(!openf[i].refs); fclose(openf[i].f); }
  rtotal=archive_file.off+archive_file.size;
  memset(openf,0,sizeof openf);
  assert(!romfs_read(archive_file.off+(6ull<<30),exact,64));
  for(unsigned i=0;i<NOPEN;i++) assert(!openf[i].f);
  fclose(archive_file.stream); archive_file.stream=NULL; rf_pinned_streams=0;
  unlink(archive);
  fclose(file);
  puts("Filesystem path and concurrent 6 GiB archive checks passed."); return 0;
}
