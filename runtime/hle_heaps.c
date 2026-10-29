/* SDK heap adapters adapted from PrimeRecomp runtime/hle/heaps.c.
   Copyright (c) 2026 Peiikai and PrimeRecomp contributors.
   MIT notice: ../LICENSES/primerecomp-MIT.txt.
   Calls the original guest implementation through the native dispatcher. */
#include "rt.h"

enum { UNIT_CREATE,UNIT_DESTROY,UNIT_ALLOC,FRAME_CREATE,FRAME_ALLOC,STD_INIT,STD_ALLOC };
typedef struct { const char* name; int operation; } HeapSymbol;
static const HeapSymbol heap_symbols[]={
  {"_ZN2nn4lmem14CreateUnitHeapEPvmmiiPNS0_6detail8HeapHeadE",UNIT_CREATE},
  {"_ZN2nn4lmem15DestroyUnitHeapEPNS0_6detail8HeapHeadE",UNIT_DESTROY},
  {"_ZN2nn4lmem20AllocateFromUnitHeapEPNS0_6detail8HeapHeadE",UNIT_ALLOC},
  {"_ZN2nn4lmem15CreateFrameHeapEPvmi",FRAME_CREATE},
  {"_ZN2nn4lmem21AllocateFromFrameHeapEPNS0_6detail8HeapHeadEmi",FRAME_ALLOC},
  {"_ZN2nn3mem17StandardAllocator10InitializeEPvm",STD_INIT},
  {"_ZN2nn3mem17StandardAllocator10InitializeEPvmb",STD_INIT},
  {"_ZN2nn3mem17StandardAllocator8AllocateEm",STD_ALLOC},
  {"_ZN2nn3mem17StandardAllocator8AllocateEmm",STD_ALLOC}
};
static struct { u64 entry; GuestFn original; const HeapSymbol* symbol; } hooks[32];
static unsigned nhooks;
static struct { u64 head,address,size; int occupied; } unit_buffers[256];
static struct { u64 caller; int operation; } failures[64];
static unsigned nfailures;
static PMutex heap_lock;
static unsigned heap_scale=1;

void hle_heaps_init(void){
  pm_init(&heap_lock);
  const char* value=getenv("SWR_UNIT_HEAP_SCALE");
  if(value){ char* end; long scale=strtol(value,&end,10); if(!*end && scale>=1 && scale<=8) heap_scale=(unsigned)scale; }
}

static void heap_failure(int operation,u64 caller,u64 size){
  pm_lock(&heap_lock);
  for(unsigned i=0;i<nfailures;i++) if(failures[i].caller==caller && failures[i].operation==operation){ pm_unlock(&heap_lock); return; }
  if(nfailures==64){ pm_unlock(&heap_lock); return; }
  failures[nfailures].caller=caller; failures[nfailures++].operation=operation;
  pm_unlock(&heap_lock);
  WARN("heap: guest allocation failed (kind %d, size/head %llx, caller %llx)\n",operation,
    (unsigned long long)size,(unsigned long long)caller);
}

static void heap_call(CPU* c,u64 entry){
  unsigned slot=0; while(slot<nhooks && hooks[slot].entry!=entry) slot++;
  if(slot==nhooks){ fatal("heap adapter: missing original at %llx",(unsigned long long)entry); return; }
  const HeapSymbol* symbol=hooks[slot].symbol;
  u64 caller=c->x[30],head=c->x[0],size=symbol->operation==STD_INIT?c->x[2]:c->x[1];
  u64 supplied_head=c->x[5];
  u64 replacement=0,mapped_size=0;
  int unit_slot=-1;
  if(symbol->operation==UNIT_CREATE && heap_scale>1 && c->x[2] && size && size<=(256ull<<20)/heap_scale){
    pm_lock(&heap_lock);
    for(unsigned i=0;i<256;i++) if(!unit_buffers[i].occupied){ unit_buffers[i].occupied=1; unit_slot=(int)i; break; }
    pm_unlock(&heap_lock);
    if(unit_slot>=0){
      mapped_size=(size*heap_scale+0xfff)&~0xfffull;
      replacement=mem_find_free(KERN_BASE,KERN_END,mapped_size+0x1000,0x1000);
      if(replacement && mem_map(replacement,mapped_size,MS_NORMAL,PERM_RW)==0){
        memset(GP(replacement),0,(size_t)mapped_size); c->x[0]=replacement; c->x[1]=size*heap_scale;
      } else replacement=0;
      if(!replacement){ pm_lock(&heap_lock); unit_buffers[unit_slot].occupied=0; pm_unlock(&heap_lock); unit_slot=-1; }
    }
  }
  hooks[slot].original(c,entry);
  while(c->pc!=caller){ u64 pc=c->pc; swr_lookup(c,pc)(c,pc); }
  c->x[30]=caller;
  if(unit_slot>=0){
    if(c->x[0] || supplied_head){
      pm_lock(&heap_lock); unit_buffers[unit_slot].head=c->x[0]?c->x[0]:supplied_head;
      unit_buffers[unit_slot].address=replacement; unit_buffers[unit_slot].size=mapped_size; pm_unlock(&heap_lock);
    } else {
      mem_unmap(replacement,mapped_size);
      pm_lock(&heap_lock); memset(&unit_buffers[unit_slot],0,sizeof unit_buffers[unit_slot]); pm_unlock(&heap_lock);
    }
  }
  if(symbol->operation==UNIT_DESTROY){
    u64 address=0,length=0;
    pm_lock(&heap_lock);
    for(unsigned i=0;i<256;i++) if(unit_buffers[i].occupied && unit_buffers[i].head==head){
      address=unit_buffers[i].address; length=unit_buffers[i].size; memset(&unit_buffers[i],0,sizeof unit_buffers[i]); break;
    }
    pm_unlock(&heap_lock); if(address) mem_unmap(address,length);
  }
  if(!c->x[0] && (symbol->operation==UNIT_ALLOC || symbol->operation==FRAME_ALLOC || (symbol->operation==STD_ALLOC && size)))
    heap_failure(symbol->operation,caller,symbol->operation==UNIT_ALLOC?head:size);
  if(symbol->operation==UNIT_CREATE || symbol->operation==FRAME_CREATE || symbol->operation==STD_INIT)
    LOGV("heap: %s size %llx from %llx -> %llx%s\n",symbol->name,(unsigned long long)size,
      (unsigned long long)caller,(unsigned long long)c->x[0],replacement?" (guest unit buffer expanded)":"");
}

GuestFn hle_heap_adapter(const char* name,u64 entry,GuestFn original){
  if(!original || original==heap_call) return NULL;
  for(unsigned i=0;i<nhooks;i++) if(hooks[i].entry==entry) return heap_call;
  if(nhooks==32) return NULL;
  for(unsigned i=0;i<sizeof heap_symbols/sizeof *heap_symbols;i++) if(!strcmp(name,heap_symbols[i].name)){
    hooks[nhooks].entry=entry; hooks[nhooks].original=original; hooks[nhooks++].symbol=&heap_symbols[i];
    return heap_call;
  }
  return NULL;
}
