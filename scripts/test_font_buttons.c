#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../runtime/font_buttons.h"
int main(int argc,char** argv){
 unsigned char src[160]={0}; fbw32(src,0x10000);fbw16(src+4,2);
 memcpy(src+12,"head",4);fbw32(src+20,44);fbw32(src+24,12);
 memcpy(src+28,"cmap",4);fbw32(src+36,56);fbw32(src+40,104);
 unsigned char* cm=src+56;fbw16(cm+2,1);fbw16(cm+4,3);fbw16(cm+6,10);fbw32(cm+8,12);
 unsigned char* m=cm+12;fbw16(m,12);fbw32(m+4,88);fbw32(m+12,6);
 unsigned cps[]={'+','A','B',0xe0e0,0x10000,0x10ffff};
 for(unsigned i=0;i<6;i++){fbw32(m+16+i*12,cps[i]);fbw32(m+20+i*12,cps[i]);fbw32(m+24+i*12,i+1);}
 size_t n=0;unsigned char* out=font_button_aliases(src,sizeof src,&n);assert(out && n>sizeof src);
 cm=out+fb32(out+36); m=cm+fb32(cm+8); unsigned char* m4=cm+fb32(cm+16);
 assert(fb_lookup(m,fb32(m+4),0xe0a0)==2);
 assert(fb_lookup(m,fb32(m+4),0xe0e0)==4); /* existing private glyph preserved */
 assert(fb_lookup(m,fb32(m+4),0xe0a1)==3);
 assert(fb_lookup(m,fb32(m+4),0xe0b3)==1);
 assert(fb_lookup(m,fb32(m+4),0x10000)==5 && fb_lookup(m,fb32(m+4),0x10ffff)==6);
 assert(fb_lookup(m4,fb16(m4+2),0xe0a0)==2 && fb_lookup(m4,fb16(m4+2),'A')==2);
 assert(fb_sum(out,n)==0xb1b0afbaU);free(out);
 for(size_t cut=0;cut<sizeof src;cut++) assert(!font_button_aliases(src,cut,&n));
 /* Exercise real fallback glyph lookup and checksum too, when available. */
 const char* path="/usr/share/fonts/TTF/DejaVuSans.ttf";FILE*f=fopen(path,"rb");
 if(!f) f=fopen("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","rb");
 if(f){fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);unsigned char* data=malloc(size);assert(fread(data,1,size,f)==size);fclose(f);
  out=font_button_aliases(data,size,&n);assert(out && fb_sum(out,n)==0xb1b0afbaU);
  if(argc==2){f=fopen(argv[1],"wb");assert(f);assert(fwrite(out,1,n,f)==n);fclose(f);}
  free(data);free(out);
 }
 puts("Font button alias checks passed.");
}
