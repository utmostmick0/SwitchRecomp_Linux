/* Build a real or temporary RomFS and fingerprint its serialized layout. */
#ifndef ROMFS_SOURCE
#define ROMFS_SOURCE "../runtime/svc_fs.c"
#endif
#include ROMFS_SOURCE
#include <assert.h>
#include <sys/stat.h>
u8* g_mem;
char g_exedir[1024],g_exename[256];
static u64 digest(u64 h,const void* data,size_t n){ const u8* p=data;while(n--){h^=*p++;h*=1099511628211ull;}return h; }
int main(int argc,char** argv){
 char temp[]="/tmp/swr-romfs-index-XXXXXX",path[2048];const char* dir;
 if(argc>1) dir=argv[1];
 else {
  assert(mkdtemp(temp));dir=temp;
  const char* dirs[]={"z-dir","a-dir"};
  for(int i=0;i<2;i++){snprintf(path,sizeof path,"%s/%s",dir,dirs[i]);assert(!mkdir(path,0700));}
  /* Reverse creation order, empty files, repeated basenames in distinct parents. */
  for(int i=1023;i>=0;i--){snprintf(path,sizeof path,"%s/%s/file-%04d",dir,i&1?"a-dir":"z-dir",i);
   FILE*f=fopen(path,"wb");assert(f);if(i)fputc(i&255,f);fclose(f);}
  snprintf(path,sizeof path,"%s/a-dir/shared",dir);FILE*f=fopen(path,"wb");assert(f);fputs("one",f);fclose(f);
  snprintf(path,sizeof path,"%s/z-dir/shared",dir);f=fopen(path,"wb");assert(f);fputs("two",f);fclose(f);
 }
 u64 started=plat_ns();romfs_build(dir);double seconds=(plat_ns()-started)/1e9;
 if(argc==1){assert(ndn==3 && nfn==1026 && nrfiles==1025);assert(!strcmp(dn[1].name,"a-dir") && !strcmp(dn[2].name,"z-dir"));}
 u64 header[10];memcpy(header,rmeta,sizeof header);u32 buckets=(u32)(header[6]/4);
 /* Every file must be reachable through its parent/name hash chain. */
 for(int i=0;i<nfn;i++){
  u32 slot=rhash(dn[fn[i].parent].off,fn[i].name)%buckets,off;memcpy(&off,rmeta+header[5]+slot*4,4);
  int found=0;
  for(int guard=0;off!=0xffffffffu && guard<=nfn;guard++){
   assert(off<header[8] && header[8]-off>=32);if(off==fn[i].off){found=1;break;}
   memcpy(&off,rmeta+header[7]+off+24,4);
  }
  assert(found);
 }
 u64 h=digest(1469598103934665603ull,rmeta,rmeta_size);
 for(int i=0;i<nrfiles;i++){h=digest(h,&rfiles[i].off,8);h=digest(h,&rfiles[i].size,8);h=digest(h,rfiles[i].path+g_romdir_len,strlen(rfiles[i].path+g_romdir_len)+1);}
 printf("RomFS benchmark: dirs=%d files=%d mapped=%d seconds=%.6f fingerprint=%016llx\n",ndn,nfn,nrfiles,seconds,(unsigned long long)h);
 if(argc==1){for(int i=0;i<nfn;i++)unlink(fn[i].host);for(int i=ndn-1;i>0;i--){snprintf(path,sizeof path,"%s/%s",dir,dn[i].name);rmdir(path);}rmdir(dir);}
 for(int i=0;i<ndn;i++)free(dn[i].name);
 for(int i=0;i<nfn;i++){free(fn[i].name);free(fn[i].host);}
 free(dn);free(fn);free(rmeta);free(rfiles);return 0;
}
