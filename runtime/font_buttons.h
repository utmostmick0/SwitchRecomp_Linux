/* Fallback-only Switch button aliases. Existing glyph outlines and advances
   stay intact; user-provided console fonts are never rewritten. */
#ifndef SWR_FONT_BUTTONS_H
#define SWR_FONT_BUTTONS_H
static unsigned fb16(const unsigned char* p){ return (unsigned)p[0]<<8|p[1]; }
static unsigned fb32(const unsigned char* p){ return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3]; }
static void fbw16(unsigned char* p,unsigned v){ p[0]=v>>8; p[1]=v; }
static void fbw32(unsigned char* p,unsigned v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static unsigned fb_sum(const unsigned char* p,size_t n){ unsigned sum=0; for(size_t i=0;i<n;i+=4){ unsigned v=0; for(unsigned j=0;j<4;j++) v=(v<<8)|(i+j<n?p[i+j]:0); sum+=v; } return sum; }
/* cmap formats 4 and 12, with strict bounds on all indirect offsets. */
static unsigned fb_lookup(const unsigned char* p,size_t n,unsigned cp){
  if(n<4) return 0;
  if(fb16(p)==12){
    if(n<16 || fb32(p+4)>n) return 0;
    unsigned count=fb32(p+12); if(count>(n-16)/12) return 0;
    unsigned lo=0,hi=count;
    while(lo<hi){ unsigned mid=lo+(hi-lo)/2; const unsigned char* g=p+16+mid*12;
      if(cp<fb32(g)) hi=mid; else if(cp>fb32(g+4)) lo=mid+1;
      else { unsigned gid=fb32(g+8)+cp-fb32(g); return gid<=65535?gid:0; } }
  } else if(fb16(p)==4 && cp<65536){
    n=fb16(p+2)<n?fb16(p+2):n; if(n<16) return 0;
    unsigned count=fb16(p+6)/2; if(!count || count>(n-16)/8) return 0;
    unsigned lo=0,hi=count;
    while(lo<hi){ unsigned mid=lo+(hi-lo)/2; if(cp>fb16(p+14+mid*2)) lo=mid+1; else hi=mid; }
    if(lo==count || cp<fb16(p+16+count*2+lo*2)) return 0;
    unsigned delta=fb16(p+16+count*4+lo*2); size_t rp=16+count*6+lo*2; unsigned off=fb16(p+rp);
    if(!off) return (cp+delta)&65535;
    size_t at=rp+off+(cp-fb16(p+16+count*2+lo*2))*2;
    if(at>n || n-at<2) return 0;
    unsigned gid=fb16(p+at); return gid?(gid+delta)&65535:0;
  }
  return 0;
}
/* Return a replacement SFNT, or NULL to retain the original on invalid input
   or allocation failure. Rebuild Unicode maps, including supplementary planes. */
static unsigned char* font_button_aliases(const unsigned char* src,size_t size,size_t* output_size){
  if(size<12 || (fb32(src)!=0x10000 && memcmp(src,"OTTO",4))) return NULL;
  unsigned nt=fb16(src+4); if(nt>(size-12)/16) return NULL;
  size_t ce=0,head=0; const unsigned char* cmap=NULL; size_t cn=0;
  for(unsigned i=0;i<nt;i++){ size_t e=12+i*16,off=fb32(src+e+8),n=fb32(src+e+12);
    if(off>size || n>size-off) return NULL;
    if(!memcmp(src+e,"cmap",4)){ ce=e; cmap=src+off; cn=n; }
    if(!memcmp(src+e,"head",4) && n>=12) head=off;
  }
  if(!ce || !head || cn<4 || fb16(cmap+2)>(cn-4)/8) return NULL;
  const unsigned char* sub=NULL; size_t sn=0; unsigned best=0;
  for(unsigned i=0;i<fb16(cmap+2);i++){
    const unsigned char* rec=cmap+4+i*8; unsigned platform=fb16(rec),enc=fb16(rec+2); size_t off=fb32(rec+4);
    if((platform!=0 && !(platform==3 && (enc==1 || enc==10))) || off>cn || cn-off<4) continue;
    unsigned fmt=fb16(cmap+off),rank=fmt==12?2:fmt==4?1:0;
    if(rank>best){ sub=cmap+off; sn=cn-off; best=rank; }
  }
  if(!sub) return NULL;
  const unsigned limit=0x110000; unsigned short* map=calloc(limit,sizeof *map); if(!map) return NULL;
  for(unsigned cp=0;cp<limit;cp++) map[cp]=(unsigned short)fb_lookup(sub,sn,cp);
  /* Private-use codepoints identify controls, not normal Unicode letters.
     Reuse readable system-font outlines only when that private glyph is absent. */
  static const unsigned short aliases[][2]={
    {0xe0a0,'A'},{0xe0a1,'B'},{0xe0a2,'X'},{0xe0a3,'Y'},
    {0xe0e0,'A'},{0xe0e1,'B'},{0xe0e2,'X'},{0xe0e3,'Y'},
    {0xe0a4,'L'},{0xe0a5,'R'},{0xe0a6,'L'},{0xe0a7,'R'},{0xe0a8,'L'},{0xe0a9,'R'},
    {0xe0e4,'L'},{0xe0e5,'R'},{0xe0e6,'L'},{0xe0e7,'R'},{0xe0e8,'L'},{0xe0e9,'R'},
    {0xe0b3,'+'},{0xe0b4,'-'},{0xe0b5,'+'},{0xe0b6,'-'},
    {0xe0ef,'+'},{0xe0f0,'-'},{0xe0f1,'+'},{0xe0f2,'-'},
    {0xe0af,0x2191},{0xe0b0,0x2193},{0xe0b1,0x2190},{0xe0b2,0x2192},
    {0xe0eb,0x2191},{0xe0ec,0x2193},{0xe0ed,0x2190},{0xe0ee,0x2192},
    {0xe0d1,0x2191},{0xe0d2,0x2193},{0xe0d3,0x2190},{0xe0d4,0x2192},
    {0xe111,0x2191},{0xe112,0x2193},{0xe113,0x2190},{0xe114,0x2192},
    {0xe0d5,0x2195},{0xe0d6,0x2194},{0xe115,0x2195},{0xe116,0x2194},
    {0xe080,0x25cf},{0xe081,'L'},{0xe082,'R'},{0xe08a,'L'},{0xe08b,'R'},
    {0xe0c0,0x25cf},{0xe0c1,'L'},{0xe0c2,'R'},{0xe0c3,0x25cf},{0xe0c4,'L'},{0xe0c5,'R'},
    {0xe100,0x25cf},{0xe101,'L'},{0xe102,'R'},{0xe103,0x25cf},{0xe104,'L'},{0xe105,'R'},
    {0xe0b9,0x2302},{0xe0f4,0x2302},{0xe0ba,0x25a1},{0xe0f5,0x25a1}
  };
  for(unsigned i=0;i<sizeof aliases/sizeof *aliases;i++) if(!map[aliases[i][0]]) map[aliases[i][0]]=map[aliases[i][1]];
  unsigned groups=0,bmp=0;
  for(unsigned cp=0;cp<limit;){ if(!map[cp]){ cp++; continue; } unsigned start=cp++;
    while(cp<limit && map[cp] && (unsigned)map[cp]==(unsigned)map[cp-1]+1 && cp!=65535) cp++;
    groups++; if(start<65535) bmp++;
  }
  /* A group crossing the BMP boundary needs a clipped format-4 counterpart. */
  size_t n4=16+(size_t)(bmp+1)*8,n12=16+(size_t)groups*12;
  if(n4>65535 || bmp>=32767 || size>(32u<<20) || n12>(32u<<20)){ free(map); return NULL; }
  size_t off=(size+3)&~(size_t)3,cmn=28+n4+n12,total=off+cmn;
  unsigned char* dst=calloc(1,(total+3)&~(size_t)3); if(!dst){ free(map); return NULL; }
  memcpy(dst,src,size); unsigned char* cm=dst+off; fbw16(cm+2,3);
  fbw16(cm+4,0); fbw16(cm+6,4); fbw32(cm+8,(unsigned)(28+n4));
  fbw16(cm+12,3); fbw16(cm+14,1); fbw32(cm+16,28);
  fbw16(cm+20,3); fbw16(cm+22,10); fbw32(cm+24,(unsigned)(28+n4));
  unsigned char* m4=cm+28; unsigned count=bmp+1; fbw16(m4,4); fbw16(m4+2,(unsigned)n4); fbw16(m4+6,count*2);
  unsigned power=1,log=0; while(power*2<=count){ power*=2;log++; }
  fbw16(m4+8,power*2); fbw16(m4+10,log); fbw16(m4+12,count*2-power*2);
  unsigned char* m12=cm+28+n4; fbw16(m12,12); fbw32(m12+4,(unsigned)n12); fbw32(m12+12,groups);
  unsigned gi=0,bi=0;
  for(unsigned cp=0;cp<limit;){ if(!map[cp]){cp++;continue;} unsigned start=cp++,gid=map[start];
    while(cp<limit && map[cp] && (unsigned)map[cp]==(unsigned)map[cp-1]+1 && cp!=65535) cp++;
    fbw32(m12+16+gi*12,start); fbw32(m12+20+gi*12,cp-1); fbw32(m12+24+gi*12,gid); gi++;
    if(start<65535){ fbw16(m4+14+bi*2,cp>65535?65534:cp-1); fbw16(m4+16+count*2+bi*2,start); fbw16(m4+16+count*4+bi*2,gid-start); bi++; }
  }
  fbw16(m4+14+bi*2,65535); fbw16(m4+16+count*2+bi*2,65535); fbw16(m4+16+count*4+bi*2,1);
  fbw32(dst+ce+8,(unsigned)off); fbw32(dst+ce+12,(unsigned)cmn); fbw32(dst+ce+4,fb_sum(cm,cmn));
  fbw32(dst+head+8,0); fbw32(dst+head+8,0xb1b0afbaU-fb_sum(dst,(total+3)&~(size_t)3));
  free(map); *output_size=(total+3)&~(size_t)3; return dst;
}
#endif
