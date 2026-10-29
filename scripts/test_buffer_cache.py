#!/usr/bin/env python3
"""Exercise the actual GL delta uploader with a bounded mock buffer."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'runtime/gpu_gl.c').read_text(); a=s.index('static int vbc_delta_upload('); b=s.index('/* range fully inside',a)
prefix=r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint64_t u64; typedef uint32_t u32; typedef unsigned char u8;
typedef unsigned GLenum; typedef intptr_t GLintptr; typedef intptr_t GLsizeiptr;
#define GL_STATIC_DRAW 0
static u64 g_vbc_mirror_bytes,st_vb_hashed,st_vb_up,bytes,calls;
static u8 gpu[1024*1024];
typedef struct { u64 len,hash; u32 stable; u8* mirror; } VbC;
static void glBufferData(GLenum t,GLsizeiptr n,const void*p,unsigned mode){(void)t;(void)mode;assert(n>=0 && n<=sizeof gpu);memcpy(gpu,p,n);bytes+=n;calls++;}
static void glBufferSubData(GLenum t,GLintptr o,GLsizeiptr n,const void*p){(void)t;assert(o>=0 && n>0 && o+n<=sizeof gpu);memcpy(gpu+o,p,n);bytes+=n;calls++;}
'''
main=r'''
int main(void){
 u8* src=calloc(1,sizeof gpu); VbC e={.len=393217};
 assert(vbc_delta_upload(0,&e,src,20)); assert(bytes==e.len);
 bytes=calls=0; assert(vbc_delta_upload(0,&e,src,20)); assert(!calls && e.stable==1);
 /* A single dirty byte in every coarse block formerly caused a full upload. */
 unsigned rows=0;
 for(u64 off=0;off<e.len;off+=65520){src[off]=1;rows++;}
 assert(vbc_delta_upload(0,&e,src,20));
 assert(bytes<=rows*20 && calls==rows && !memcmp(src,gpu,e.len) && !memcmp(src,e.mirror,e.len));
 bytes=calls=0; src[40]=2;src[60]=3;
 assert(vbc_delta_upload(0,&e,src,20)); assert(bytes==40 && calls==1);
 bytes=calls=0; src[e.len-1]=4;
 assert(vbc_delta_upload(0,&e,src,20)); assert(bytes==17 && calls==1);
 free(e.mirror);g_vbc_mirror_bytes=0;e=(VbC){.len=1003};
 memset(src,0,1003);assert(vbc_delta_upload(0,&e,src,0));bytes=calls=0;
 src[503]=7;assert(vbc_delta_upload(0,&e,src,0));assert(bytes==1 && calls==1);
 for(unsigned round=0;round<100;round++){
  unsigned off=(round*719)%1003;src[off]++;
  assert(vbc_delta_upload(0,&e,src,13));assert(!memcmp(src,gpu,e.len) && !memcmp(src,e.mirror,e.len));
 }
 free(e.mirror);free(src);puts("Buffer delta upload checks passed.");
}
'''
with tempfile.TemporaryDirectory(prefix='swr-buffer-check-') as d:
 p=Path(d);(p/'test.c').write_text(prefix+s[a:b]+main)
 subprocess.run(['gcc','-fsanitize=address,undefined','-g',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
