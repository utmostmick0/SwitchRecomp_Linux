/* Small dependency-free ZIP writer for Linux trace captures. ZIP method 0
   stores files unchanged; the archive is a single portable transfer unit. */
#include "trace_zip.h"
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { char name[512]; uint32_t crc,size,offset; } TraceZipEntry;
static TraceZipEntry* entries;
static size_t count,capacity;

static void put16(FILE* f,uint32_t v){ fputc((int)v&255,f); fputc((int)(v>>8)&255,f); }
static void put32(FILE* f,uint32_t v){ put16(f,v); put16(f,v>>16); }

static int file_crc(const char* path,uint32_t* crc_out,uint32_t* size_out){
  FILE* f=fopen(path,"rb"); if(!f) return 0;
  unsigned char buf[32768]; size_t n; uint32_t crc=~0u,total=0;
  while((n=fread(buf,1,sizeof buf,f))!=0){
    if(n>UINT32_MAX-total){ fclose(f); return 0; }
    total+=(uint32_t)n;
    for(size_t i=0;i<n;i++){ crc^=buf[i]; for(int b=0;b<8;b++) crc=(crc>>1)^(0xEDB88320u&-(crc&1)); }
  }
  if(ferror(f)){ fclose(f); return 0; }
  fclose(f); *crc_out=~crc; *size_out=total; return 1;
}

static int collect(const char* root,const char* rel){
  char dirpath[PATH_MAX];
  if(snprintf(dirpath,sizeof dirpath,"%s/%s",root,rel)>=(int)sizeof dirpath) return 0;
  DIR* d=opendir(dirpath); if(!d) return 0;
  struct dirent* de; int ok=1;
  while(ok && (de=readdir(d))){
    if(!strcmp(de->d_name,".")||!strcmp(de->d_name,"..")) continue;
    char child[PATH_MAX],name[512];
    if(snprintf(child,sizeof child,"%s/%s",dirpath,de->d_name)>=(int)sizeof child ||
       snprintf(name,sizeof name,"%s%s%s",rel,*rel?"/":"",de->d_name)>=(int)sizeof name){ ok=0; break; }
    struct stat st; if(lstat(child,&st)){ ok=0; break; }
    if(S_ISDIR(st.st_mode)){ ok=collect(root,name); continue; }
    if(!S_ISREG(st.st_mode)) continue;
    if(count>=65535 || (count==capacity && capacity>SIZE_MAX/(2*sizeof *entries))){ ok=0; break; }
    if(count==capacity){ size_t cap=capacity?capacity*2:64; TraceZipEntry* p=realloc(entries,cap*sizeof *p); if(!p){ ok=0; break; } entries=p; capacity=cap; }
    TraceZipEntry* e=&entries[count]; memset(e,0,sizeof *e); strcpy(e->name,name);
    if(!file_crc(child,&e->crc,&e->size)){ ok=0; break; }
    count++;
  }
  closedir(d); return ok;
}

int trace_zip_create(const char* directory,char* archive,unsigned archive_size){
  if(!directory || !archive || !archive_size || snprintf(archive,archive_size,"%s.zip",directory)>=(int)archive_size) return 0;
  count=0;
  if(!collect(directory,"")) goto fail;
  FILE* out=fopen(archive,"wb"); if(!out) goto fail;
  for(size_t i=0;i<count;i++){
    TraceZipEntry* e=&entries[i]; size_t nl=strlen(e->name); char path[PATH_MAX];
    if(snprintf(path,sizeof path,"%s/%s",directory,e->name)>=(int)sizeof path) goto write_fail;
    long pos=ftell(out); if(pos<0 || (unsigned long)pos>UINT32_MAX) goto write_fail; e->offset=(uint32_t)pos;
    put32(out,0x04034b50); put16(out,20); put16(out,0x0800); put16(out,0); put16(out,0); put16(out,0x21);
    put32(out,e->crc); put32(out,e->size); put32(out,e->size); put16(out,(uint32_t)nl); put16(out,0);
    if(fwrite(e->name,1,nl,out)!=nl) goto write_fail;
    FILE* in=fopen(path,"rb"); if(!in) goto write_fail;
    unsigned char buf[32768]; size_t n; uint32_t copied=0;
    while((n=fread(buf,1,sizeof buf,in))!=0){
      if(copied>e->size || n>e->size-copied || fwrite(buf,1,n,out)!=n){ fclose(in); goto write_fail; }
      copied+=(uint32_t)n;
    }
    if(ferror(in)||copied!=e->size){ fclose(in); goto write_fail; }
    fclose(in);
  }
  { long start=ftell(out); if(start<0 || (unsigned long)start>UINT32_MAX) goto write_fail;
    for(size_t i=0;i<count;i++){ TraceZipEntry* e=&entries[i]; size_t nl=strlen(e->name);
      put32(out,0x02014b50); put16(out,20); put16(out,20); put16(out,0x0800); put16(out,0); put16(out,0); put16(out,0x21);
      put32(out,e->crc); put32(out,e->size); put32(out,e->size); put16(out,(uint32_t)nl); put16(out,0); put16(out,0); put16(out,0); put16(out,0); put32(out,0); put32(out,e->offset);
      if(fwrite(e->name,1,nl,out)!=nl) goto write_fail;
    }
    long end=ftell(out); if(end<start || (unsigned long)end>UINT32_MAX) goto write_fail;
    put32(out,0x06054b50); put16(out,0); put16(out,0); put16(out,(uint32_t)count); put16(out,(uint32_t)count);
    put32(out,(uint32_t)(end-start)); put32(out,(uint32_t)start); put16(out,0);
  }
  if(fclose(out)){ remove(archive); goto fail; }
  free(entries); entries=NULL; count=capacity=0; return 1;
write_fail:
  fclose(out); remove(archive);
fail:
  free(entries); entries=NULL; count=capacity=0; archive[0]=0; return 0;
}
