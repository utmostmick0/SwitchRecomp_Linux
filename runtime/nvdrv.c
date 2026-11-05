/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* nvdrv: nvmap, nvhost-as-gpu, nvhost-ctrl, nvhost-ctrl-gpu, nvhost-gpu. enough for NVN and deko3d */
#include "rt.h"
#include "native_gpu.h"

/* ============================================================ nvmap */
typedef struct { u64 size; u32 align, heap, flags; u8 kind; u64 addr; u32 id; int alive; u64 native_owner; u32 native_refs,native_map_refs; } NvMapH;

static NvMapH nvh[8192]; static u32 nvh_next=1; static PMutex g_nvlock;
typedef struct { u64 owner,address,size; u32 memory,uses; } NativeMap;
typedef struct { u64 owner; u32 id,map; } NativeLease;
static NativeMap native_maps[4096];
static NativeLease native_leases[4096];
static u64 native_owners[128],native_owner_next;
static u32 native_owner_refs[128];
static u32 native_id_next=0x80000000u,native_lease_next;
static u32 nvmap_slot(void){
  if(nvh_next<8192) return nvh_next++;
  for(u32 i=1;i<8192;i++) if(!nvh[i].alive) return i;
  return 0;
}
static int native_overlap(u64 address,u64 size){
  for(unsigned i=0;i<4096;i++) if(native_maps[i].owner &&
      address<native_maps[i].address+native_maps[i].size && native_maps[i].address<address+size) return 1;
  return 0;
}

u64 nvmap_addr(u32 h){ if(h>=8192 || nvh[h].native_owner) return 0; return nvh[h].addr; }

u32 nvmap_size(u32 h){ if(h>=8192 || nvh[h].native_owner) return 0; return (u32)nvh[h].size; }

static u32 nvmap_from_id(u32 id){ for(u32 i=1;i<nvh_next;i++) if(nvh[i].alive && nvh[i].id==id) return i; return 0; }

u64 nvmap_addr_by_id(u32 id){ u32 h=nvmap_from_id(id); if(!h) h=id<8192? id : 0; return nvmap_addr(h); }

/* ============================================================ GPU VA space (49 bit) */

/* two level table, gva -> guest address, 4K granularity */
#define GPAGE_BITS 12
#define GL1 (1u<<16)     /* 40-12 = 28 bits, split 16 + 12 */
static u64** gmmu; /* low 40-bit bank, retained for the existing NS1 layout */
static u64** gmmu_high[511]; /* remaining nine address bits; allocated sparsely */
static u32 gmmu_gen;
u32 gmmu_generation(void){ return __atomic_load_n(&gmmu_gen,__ATOMIC_ACQUIRE); }
static u64** gmmu_bank(u64 address,int create){
  if(address>=(1ull<<49)) return NULL;
  u32 bank=(u32)(address>>40);
  u64*** slot=bank?&gmmu_high[bank-1]:&gmmu;
  u64** table=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
  if(!table && create){
    u64** fresh=calloc(GL1,sizeof *fresh); if(!fresh) return NULL;
    if(!__atomic_compare_exchange_n(slot,&table,fresh,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)) free(fresh);
    else table=fresh;
  }
  return table;
}
u64 gmmu_translate(u64 gva){
  u64** bank=gmmu_bank(gva,0); if(!bank) return 0;
  u64 p=gva>>GPAGE_BITS; u32 i=(u32)(p>>12)&(GL1-1), j=(u32)p&0xFFF;
  u64* t=__atomic_load_n(&bank[i],__ATOMIC_ACQUIRE); if(!t) return 0;
  u64 e=__atomic_load_n(&t[j],__ATOMIC_ACQUIRE); if(!(e&1)) return 0; return (e&~1ull)+(gva&0xFFF); }

static int gmmu_map_impl(u64 gva, u64 cpu, u64 size){
  if(gva>=(1ull<<49) || size>(1ull<<49)-gva || cpu>=AS_SIZE || size>AS_SIZE-cpu) return 0;
  /* Allocate every page-table node before publishing any mapping. A failed
     allocation must not leave half of a native memory transaction installed. */
  for(u64 o=0;o<size;o+=0x1000){
    u64** bank=gmmu_bank(gva+o,1); if(!bank) return 0;
    u32 i=(u32)((gva+o)>>24)&(GL1-1);
    if(!__atomic_load_n(&bank[i],__ATOMIC_ACQUIRE)){
      u64* fresh=calloc(4096,sizeof *fresh); if(!fresh) return 0; u64* expected=NULL;
      if(!__atomic_compare_exchange_n(&bank[i],&expected,fresh,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)) free(fresh);
    }
  }
  if(size) __atomic_add_fetch(&gmmu_gen,1,__ATOMIC_ACQ_REL); /* odd: a mapping update is in progress */
  for(u64 o=0;o<size;o+=0x1000){ u64 p=(gva+o)>>GPAGE_BITS; u32 i=(u32)(p>>12)&(GL1-1), j=(u32)p&0xFFF;
    u64** bank=gmmu_bank(gva+o,0); __atomic_store_n(&bank[i][j],(cpu+o)|1,__ATOMIC_RELEASE); }
  if(size) __atomic_add_fetch(&gmmu_gen,1,__ATOMIC_RELEASE); /* even: mapping is stable */
  return 1; }

static void gmmu_unmap_impl(u64 gva, u64 size){
  if(gva>=(1ull<<49) || size>(1ull<<49)-gva) return;
  if(size) __atomic_add_fetch(&gmmu_gen,1,__ATOMIC_ACQ_REL);
  for(u64 o=0;o<size;o+=0x1000){ u64 p=(gva+o)>>GPAGE_BITS; u32 i=(u32)(p>>12)&(GL1-1), j=(u32)p&0xFFF;
    u64** bank=gmmu_bank(gva+o,0); u64* table=bank?__atomic_load_n(&bank[i],__ATOMIC_ACQUIRE):NULL;
    if(table) __atomic_store_n(&table[j],0,__ATOMIC_RELEASE); }
  if(size) __atomic_add_fetch(&gmmu_gen,1,__ATOMIC_RELEASE); }

/* Serialize writers so the generation's odd/even contract remains meaningful.
   Legacy NVDRV calls cannot overwrite or unmap a native owner's allocation. */
static int gmmu_map(u64 gva,u64 cpu,u64 size){
  if(gva>=(1ull<<49) || size>(1ull<<49)-gva) return 0;
  pm_lock(&g_nvlock); int ok=!native_overlap(gva,size) && gmmu_map_impl(gva,cpu,size);
  pm_unlock(&g_nvlock); return ok;
}
static int gmmu_unmap(u64 gva,u64 size){
  if(gva>=(1ull<<49) || size>(1ull<<49)-gva) return 0;
  pm_lock(&g_nvlock); int ok=!native_overlap(gva,size);
  if(ok) gmmu_unmap_impl(gva,size); pm_unlock(&g_nvlock); return ok;
}

static int native_owner_valid(u64 owner){
  if(!owner) return 0;
  for(unsigned i=0;i<128;i++) if(native_owners[i]==owner) return 1;
  return 0;
}
static u32 native_memory(u64 owner,u32 id){
  if(!native_owner_valid(owner)) return 0;
  u32 h=nvmap_from_id(id);
  return h && nvh[h].native_owner==owner && nvh[h].native_refs?h:0;
}
u64 nv_native_owner_open(void){
  pm_lock(&g_nvlock); u64 owner=0;
  if(native_owner_next!=UINT64_MAX) for(unsigned i=0;i<128;i++) if(!native_owners[i]){
    owner=native_owners[i]=++native_owner_next; native_owner_refs[i]=1; break;
  }
  pm_unlock(&g_nvlock); return owner;
}
int nv_native_owner_close(u64 owner){
  pm_lock(&g_nvlock);
  if(!native_owner_valid(owner)){ pm_unlock(&g_nvlock); return NG_NOT_FOUND; }
  for(unsigned i=0;i<128;i++) if(native_owners[i]==owner && native_owner_refs[i]!=1){ pm_unlock(&g_nvlock); return NG_BUSY; }
  for(u32 h=1;h<nvh_next;h++) if(nvh[h].alive && nvh[h].native_owner==owner){ pm_unlock(&g_nvlock); return NG_BUSY; }
  for(unsigned i=0;i<128;i++) if(native_owners[i]==owner) native_owners[i]=0;
  pm_unlock(&g_nvlock); return NG_OK;
}
int nv_native_owner_retain(u64 owner){
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  for(unsigned i=0;owner && i<128;i++) if(native_owners[i]==owner){
    result=NG_LIMIT; if(native_owner_refs[i]!=UINT32_MAX){ native_owner_refs[i]++; result=NG_OK; } break;
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_owner_drop(u64 owner){
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  for(unsigned i=0;owner && i<128;i++) if(native_owners[i]==owner){
    result=NG_INVALID; if(native_owner_refs[i]>1){ native_owner_refs[i]--; result=NG_OK; } break;
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_memory_register(u64 owner,u64 address,u64 size,u32* out){
  if(!out || !address || !size || (address|size)&0xfff || size>(12ull<<30) || address>=AS_SIZE || size>AS_SIZE-address) return NG_INVALID;
  pm_lock(&g_nvlock);
  if(!native_owner_valid(owner)){ pm_unlock(&g_nvlock); return NG_NOT_FOUND; }
  if(native_id_next==UINT32_MAX){ pm_unlock(&g_nvlock); return NG_LIMIT; }
  if(mem_device_pin(address,size)){ pm_unlock(&g_nvlock); return NG_UNMAPPED; }
  u32 h=nvmap_slot();
  if(!h){ mem_device_unpin(address,size); pm_unlock(&g_nvlock); return NG_LIMIT; }
  NvMapH* memory=&nvh[h]; memset(memory,0,sizeof *memory);
  memory->size=size; memory->addr=address; memory->align=0x1000;
  memory->id=++native_id_next; memory->alive=1; memory->native_owner=owner; memory->native_refs=1;
  *out=memory->id; pm_unlock(&g_nvlock); return NG_OK;
}
int nv_native_memory_info(u64 owner,u32 id,u64* address,u64* size){
  if(!address || !size) return NG_INVALID;
  pm_lock(&g_nvlock); u32 h=native_memory(owner,id);
  if(h){ *address=nvh[h].addr; *size=nvh[h].size; }
  pm_unlock(&g_nvlock); return h?NG_OK:NG_NOT_FOUND;
}
int nv_native_memory_retain(u64 owner,u32 id){
  pm_lock(&g_nvlock); u32 h=native_memory(owner,id); int result=NG_NOT_FOUND;
  if(h){ result=NG_LIMIT; if(nvh[h].native_refs!=UINT32_MAX){ nvh[h].native_refs++; result=NG_OK; } }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_memory_release(u64 owner,u32 id){
  pm_lock(&g_nvlock); u32 h=native_memory(owner,id); int result=NG_NOT_FOUND;
  if(h){
    result=NG_OK;
    if(nvh[h].native_refs==1 && !nvh[h].native_map_refs && mem_device_unpin(nvh[h].addr,nvh[h].size)) result=NG_BUSY;
    else if(!--nvh[h].native_refs && !nvh[h].native_map_refs) memset(&nvh[h],0,sizeof nvh[h]);
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_map(u64 owner,u32 id,u64 gpu,u64 offset,u64 size){
  if(!size || (gpu|offset|size)&0xfff || gpu>=(1ull<<49) || size>(1ull<<49)-gpu) return NG_INVALID;
  pm_lock(&g_nvlock); u32 h=native_memory(owner,id);
  if(!h){ pm_unlock(&g_nvlock); return NG_NOT_FOUND; }
  if(offset>nvh[h].size || size>nvh[h].size-offset){ pm_unlock(&g_nvlock); return NG_INVALID; }
  int slot=-1;
  for(unsigned i=0;i<4096;i++) if(!native_maps[i].owner){ slot=(int)i; break; }
  if(slot<0 || nvh[h].native_map_refs==UINT32_MAX){ pm_unlock(&g_nvlock); return NG_LIMIT; }
  for(u64 off=0;off<size;off+=0x1000) if(gmmu_translate(gpu+off)){ pm_unlock(&g_nvlock); return NG_BUSY; }
  if(!gmmu_map_impl(gpu,nvh[h].addr+offset,size)){ pm_unlock(&g_nvlock); return NG_LIMIT; }
  nvh[h].native_map_refs++;
  native_maps[slot]=(NativeMap){owner,gpu,size,h,0};
  pm_unlock(&g_nvlock); return NG_OK;
}
int nv_native_unmap(u64 owner,u64 gpu,u64 size){
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  if(native_owner_valid(owner)) for(unsigned i=0;i<4096;i++){
    NativeMap* map=&native_maps[i];
    if(map->owner==owner && map->address==gpu && map->size==size){
      if(map->uses){ result=NG_BUSY; break; }
      NvMapH* memory=&nvh[map->memory];
      /* The mapping owns the final physical pin if its guest handle is gone. */
      if(!memory->native_refs && memory->native_map_refs==1 && mem_device_unpin(memory->addr,memory->size)){ result=NG_BUSY; break; }
      gmmu_unmap_impl(gpu,size);
      if(!--memory->native_map_refs && !memory->native_refs) memset(memory,0,sizeof *memory);
      memset(map,0,sizeof *map); result=NG_OK; break;
    }
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_range_acquire(u64 owner,u64 gpu,u64 size,u32* out){
  if(!out || !size || gpu>=(1ull<<49) || size>(1ull<<49)-gpu) return NG_INVALID;
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  if(native_owner_valid(owner)) for(unsigned i=0;i<4096;i++){
    NativeMap* map=&native_maps[i];
    if(map->owner!=owner || gpu<map->address || gpu-map->address>map->size || size>map->size-(gpu-map->address)) continue;
    result=NG_LIMIT;
    if(native_lease_next!=UINT32_MAX && map->uses!=UINT32_MAX) for(unsigned j=0;j<4096;j++) if(!native_leases[j].owner){
      native_leases[j]=(NativeLease){owner,++native_lease_next,i}; map->uses++;
      *out=native_leases[j].id; result=NG_OK; break;
    }
    break;
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_range_release(u64 owner,u32 lease){
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  if(native_owner_valid(owner)) for(unsigned i=0;i<4096;i++) if(native_leases[i].owner==owner && native_leases[i].id==lease){
    native_maps[native_leases[i].map].uses--; memset(&native_leases[i],0,sizeof native_leases[i]); result=NG_OK; break;
  }
  pm_unlock(&g_nvlock); return result;
}
int nv_native_range_address(u64 owner,u64 gpu,u64 size,u64* out){
  if(!out || !size || gpu>=(1ull<<49) || size>(1ull<<49)-gpu) return NG_INVALID;
  pm_lock(&g_nvlock); int result=NG_NOT_FOUND;
  if(native_owner_valid(owner)) for(unsigned i=0;i<4096;i++){
    NativeMap* map=&native_maps[i];
    if(map->owner==owner && gpu>=map->address && gpu-map->address<=map->size && size<=map->size-(gpu-map->address)){
      /* Range readers must hold a lease so the returned backing cannot retire. */
      if(!map->uses){ result=NG_BUSY; break; }
      *out=gmmu_translate(gpu); result=NG_OK; break;
    }
  }
  pm_unlock(&g_nvlock); return result;
}

typedef struct { u64 gva, size; u32 h; u64 off; } GMap;

static GMap gmaps[16384]; static int ngmaps;

/* a mapping stops at the end of its nvmap handle. games map small buffers with a bigger size (12 KB as a 64 KB big
   page) and unmap the guest page right after the buffer as a guard. on the console the rest of the page is other
   memory, here it would point the GPU at that hole and crash the GPU thread */
static int gmmu_map_handle(u64 gva, u32 h, u64 off, u64 size){
  if(gva>=(1ull<<49) || size>(1ull<<49)-gva) return 0;
  pm_lock(&g_nvlock);
  if(native_overlap(gva,size)){ pm_unlock(&g_nvlock); return 0; }
  u64 hs=((u64)nvh[h].size+0xFFF)&~0xFFFull, n= off<hs? hs-off : 0; if(n>size) n=size;
  int ok=gmmu_map_impl(gva,nvh[h].addr+off,n);
  if(ok && n<size) gmmu_unmap_impl(gva+n,size-n);
  pm_unlock(&g_nvlock); return ok; }

static u64 g_va_next=0x0400000000ull;       /* dumb bump allocator for gva */
static u64 va_alloc(u64 size, u64 align){ if(align<0x10000) align=0x10000; u64 a=(g_va_next+align-1)&~(align-1); g_va_next=a+((size+0xFFFF)&~0xFFFFull); return a; }

/* ============================================================ syncpoints */
static u32 syncpt[256]; static u32 syncpt_max[256]; static PCond g_sync_cv; static PMutex g_sync_lk;

u32 syncpt_read(u32 id){ return __atomic_load_n(&syncpt[id&255],__ATOMIC_SEQ_CST); }

void syncpt_incr(u32 id){ pm_lock(&g_sync_lk); syncpt[id&255]++; pc_broadcast(&g_sync_cv); pm_unlock(&g_sync_lk);
  extern void nv_events_check(void); nv_events_check(); }

int syncpt_wait(u32 id, u32 thresh, i64 timeout){
  pm_lock(&g_sync_lk); u64 end=plat_ns()+(u64)(timeout<0?0:timeout);
  while((i32)(syncpt[id&255]-thresh)<0){
    if(timeout==0){ pm_unlock(&g_sync_lk); return 0; }
    i64 rem=-1; if(timeout>0){ u64 now=plat_ns(); if(now>=end){ pm_unlock(&g_sync_lk); return 0; } rem=(i64)(end-now); }
    pc_wait(&g_sync_cv,&g_sync_lk,rem); }
  pm_unlock(&g_sync_lk); return 1; }

u32 syncpt_alloc(void){ static u32 next=20; u32 id=next++; if(next>=192) next=20; return id; }   /* 20..191, the hw has 192. the video channels take theirs from here too */

/* nvhost-ctrl events = async syncpoint waits */
typedef struct { KEvent* ev; u32 id, thresh; volatile int armed; } NvEvent;

static NvEvent nvev[64];
/* arming (game thread) races with nv_events_check (GPU/video threads after an incr). I clear the event before
   arming and keep both under one lock, if not an incr in between signals it, the clear wipes that signal and the
   game never wakes up */
static volatile u32 g_nvev_lk;

static void nvev_lock(void){ while(__atomic_exchange_n(&g_nvev_lk,1,__ATOMIC_ACQUIRE)){ while(__atomic_load_n(&g_nvev_lk,__ATOMIC_RELAXED)) ; } }

static void nvev_unlock(void){ __atomic_store_n(&g_nvev_lk,0,__ATOMIC_RELEASE); }

/* Every returned async token must refer to one stable, retained event object. */
static KEvent* nv_event_get(u32 slot){
  slot&=63; nvev_lock();
  if(!nvev[slot].ev){ nvev[slot].ev=kevent_new("nvhost event",0); kobj_ref(&nvev[slot].ev->o); }
  KEvent* event=nvev[slot].ev; nvev_unlock(); return event;
}

void nv_events_check(void){
  nvev_lock();
  for(int i=0;i<64;i++) if(nvev[i].armed && (i32)(syncpt_read(nvev[i].id)-nvev[i].thresh)>=0){ nvev[i].armed=0; kevent_signal(nvev[i].ev); }
  nvev_unlock(); }

/* ============================================================ files (fds) */
enum { DEV_NONE, DEV_NVMAP, DEV_AS, DEV_CTRL, DEV_CTRLGPU, DEV_GPU, DEV_NVDEC, DEV_VIC, DEV_OTHER };

u32 host1x_ioctl(int is_vic, u32 cmd, u8* d, u32 n);
typedef struct { int dev; u32 syncpt; u64 user_data; u32 class_ids[8]; int nclass; KEvent* ev_sm, *ev_err; } NvFile;

static NvFile nvf[256];

static u32 ioctl_nvmap(u32 cmd, u8* d, u32 n){
  (void)n;
  switch(cmd&0xFF){
    case 0x01: { u32 size; memcpy(&size,d,4); pm_lock(&g_nvlock);
      /* reuse freed handles once the table is full instead of writing past it */
      u32 h=nvmap_slot();
      if(!h){ pm_unlock(&g_nvlock); return 1; }
      memset(&nvh[h],0,sizeof nvh[h]); nvh[h].size=size; nvh[h].id=h; nvh[h].alive=1; pm_unlock(&g_nvlock); memcpy(d+4,&h,4); return 0; }
    case 0x03: { u32 id; memcpy(&id,d,4); u32 h=nvmap_from_id(id); if(!h || nvh[h].native_owner) return 1; memcpy(d+4,&h,4); return 0; }
    case 0x04: { u32 h; memcpy(&h,d,4); if(!h||h>=8192 || nvh[h].native_owner) return 1; NvMapH* m=&nvh[h];
      memcpy(&m->heap,d+4,4); memcpy(&m->flags,d+8,4); memcpy(&m->align,d+12,4); m->kind=d[16]; memcpy(&m->addr,d+24,8);
      LOGV("nvmap alloc h=%u size=%x addr=%llx\n",h,m->size,(unsigned long long)m->addr); return 0; }
    case 0x05: { u32 h; memcpy(&h,d,4); if(h<8192 && nvh[h].native_owner) return 1; if(h&&h<8192){ u64 a=nvh[h].addr; u32 s=nvh[h].size, f=nvh[h].flags; memcpy(d+8,&a,8); memcpy(d+16,&s,4); memcpy(d+20,&f,4); nvh[h].alive=0; } return 0; }
    case 0x09: { u32 h, p, r=0; memcpy(&h,d,4); memcpy(&p,d+4,4); if(h>=8192) return 1; NvMapH* m=&nvh[h];
      if(m->native_owner) return 1;
      switch(p){ case 1: r=m->size; break; case 2: r=m->align?m->align:0x1000; break; case 3: r=0; break; case 4: r=0x40000000; break; case 5: r=m->kind; break; case 6: r=0; break; }
      memcpy(d+8,&r,4); return 0; }
    case 0x0E: { u32 h; memcpy(&h,d+4,4); if(h<8192 && nvh[h].native_owner) return 1; u32 id= h<8192? nvh[h].id : 0; memcpy(d,&id,4); return 0; }
  }
  WARN("nvmap ioctl %x\n",cmd); return 0; }

static u32 g_big_page_bits=16;     /* 64K unless asked otherwise */

static u32 ioctl_as(u32 cmd, u8* d, u32 n, u8* extra, u32 nextra){
  (void)n;
  switch(cmd&0xFF){
    case 0x01: return 0;
    case 0x02: { u32 pages, psz, flags; u64 off; memcpy(&pages,d,4); memcpy(&psz,d+4,4); memcpy(&flags,d+8,4); memcpy(&off,d+16,8);
      u64 size=(u64)pages*psz; u64 a= (flags&1)? off : va_alloc(size,off>psz? off : psz); memcpy(d+16,&a,8);
      LOGV("as alloc_space %llx size %llx\n",(unsigned long long)a,(unsigned long long)size); return 0; }
    case 0x03: { u64 off; u32 pages, psz; memcpy(&off,d,8); memcpy(&pages,d+8,4); memcpy(&psz,d+12,4); return !gmmu_unmap(off,(u64)pages*psz); }
    case 0x05: { u64 off; memcpy(&off,d,8); for(int i=0;i<ngmaps;i++) if(gmaps[i].gva==off){ if(!gmmu_unmap(off,gmaps[i].size)) return 1; gmaps[i]=gmaps[--ngmaps]; break; } return 0; }
    case 0x06: { u32 flags, kind, h, psz; i64 boff; u64 msize, off; memcpy(&flags,d,4); memcpy(&kind,d+4,4); memcpy(&h,d+8,4); memcpy(&psz,d+12,4);
      memcpy(&boff,d+16,8); memcpy(&msize,d+24,8); memcpy(&off,d+32,8);
      if((flags&0x100) && h==0) return 0;   /* just updates kind on an existing map */
      if(h>=8192 || !nvh[h].alive || nvh[h].native_owner){ WARN("as map: invalid nvmap handle %u flags=%x kind=%x psz=%x boff=%llx msize=%llx off=%llx n=%u\n",h,flags,kind,psz,(unsigned long long)boff,(unsigned long long)msize,(unsigned long long)off,n); return 1; }
      if(flags&0x100){ /* modify: remap inside an existing mapping */
        return !gmmu_map_handle(off,h,(u64)boff,msize?msize:nvh[h].size); }
      u64 size= msize? msize : nvh[h].size;
      u64 gva= (flags&1)? off : va_alloc(size,psz?psz:0x10000);
      if(!gmmu_map_handle(gva,h,(u64)boff,size)) return 1;
      if(ngmaps<16384){ gmaps[ngmaps].gva=gva; gmaps[ngmaps].size=size; gmaps[ngmaps].h=h; gmaps[ngmaps].off=(u64)boff; ngmaps++; }
      memcpy(d+32,&gva,8);
      LOGV("as map h=%u cpu=%llx gva=%llx size=%llx\n",h,(unsigned long long)(nvh[h].addr+boff),(unsigned long long)gva,(unsigned long long)size);
      return 0; }
    case 0x08: { u32 bufsz=2*24; memcpy(d+8,&bufsz,4); u64 r[6]={0x04000000,0x1000,0x3FFBFFF, 0x0400000000ull,0x10000,0x1BFFF};
      u8* rg=d+16; u64 o0=0x04000000; u32 ps0=0x1000; u64 np0=0x3FFBFFF; u64 o1=0x0400000000ull; u32 ps1=1u<<g_big_page_bits; u64 np1=0x1BFFF;
      memcpy(rg,&o0,8); memcpy(rg+8,&ps0,4); memset(rg+12,0,4); memcpy(rg+16,&np0,8);
      memcpy(rg+24,&o1,8); memcpy(rg+32,&ps1,4); memset(rg+36,0,4); memcpy(rg+40,&np1,8); (void)r;
      if(extra && nextra) memcpy(extra,rg,nextra<48? nextra : 48); return 0; }
    case 0x09: { /* ALLOC_AS_EX: {u32 flags, i32 as_fd, u32 big_page_size, u32 pad, u64 va_start, va_end, va_split}
                    big_page_size 0 means 128K (yuzu DEFAULT_BIG_PAGE_SIZE). REMAP uses big page units */
      u32 bps; memcpy(&bps,d+8,4); if(!bps) bps=0x20000;
      if(bps==0x10000 || bps==0x20000) g_big_page_bits= bps==0x20000? 17 : 16;
      LOG("nvdrv: GPU address space init (big page field %x) -> %u KB big pages\n",bps,(1u<<g_big_page_bits)>>10); return 0; }
    case 0x14: { /* REMAP entries: {u16 flags, u16 kind, u32 handle, u32 mem_off, u32 gpu_off, u32 pages}, offsets in big pages */
      u32 cnt=n/20; for(u32 i=0;i<cnt;i++){ u8* e=d+i*20; u32 h, moff, goff, pages; memcpy(&h,e+4,4); memcpy(&moff,e+8,4); memcpy(&goff,e+12,4); memcpy(&pages,e+16,4);
        /* big page size comes from ALLOC_AS_EX */
        u64 gva=(u64)goff<<g_big_page_bits, size=(u64)pages<<g_big_page_bits;
        if(!h) gmmu_unmap(gva,size); else if(h<8192 && !nvh[h].native_owner) gmmu_map_handle(gva,h,(u64)moff<<g_big_page_bits,size);
        { static int said; if(said<4){ said++; LOG("nvdrv: remap handle %u: %llx bytes at gpu %llx (big page %u KB)\n",h,(unsigned long long)size,(unsigned long long)gva,(1u<<g_big_page_bits)>>10); } } }
      return 0; }
  }
  (void)extra; (void)nextra; WARN("as ioctl %x\n",cmd); return 0; }

static u32 ioctl_ctrl(u32 cmd, u8* d, u32 n){
  (void)n;
  switch(cmd&0xFF){
    case 0x14: { u32 id; memcpy(&id,d,4); u32 v=syncpt_read(id); memcpy(d+4,&v,4); return 0; }
    case 0x15: { u32 id; memcpy(&id,d,4); syncpt_incr(id); return 0; }
    case 0x16: case 0x19: { u32 id, th; i32 to; memcpy(&id,d,4); memcpy(&th,d+4,4); memcpy(&to,d+8,4);
      int ok=syncpt_wait(id,th,to<0? -1 : (i64)to*1000000); if(n>=16){ u32 v=syncpt_read(id); memcpy(d+12,&v,4); } return ok? 0 : 5; }
    case 0x1A: { u32 id; memcpy(&id,d,4); u32 v=syncpt_max[id&255]; memcpy(d+4,&v,4); return 0; }
    case 0x1B: { memset(d+0x41,0,0x41); return 0; }
    case 0x1C: { /* EVENT_SIGNAL: cancel the pending wait and wake the waiter. NVN does this before reusing an
                    event and the waiter stays blocked if I ignore it */
      u32 ev; memcpy(&ev,d,4); u32 slot=ev&63;
      nvev_lock(); if(nvev[slot].ev){ nvev[slot].armed=0; kevent_signal(nvev[slot].ev); } nvev_unlock();
      { static int said; if(said<2){ said++; LOG("nvdrv: event %u signaled by the game\n",slot); } }
      return 0; }
    case 0x1D: case 0x1E: { if(n<16) return 1; u32 id, th; i32 to; u32 ev; memcpy(&id,d,4); memcpy(&th,d+4,4); memcpy(&to,d+8,4); memcpy(&ev,d+12,4);
      if(syncpt_wait(id,th,0)){ u32 v=syncpt_read(id); memcpy(d+12,&v,4); return 0; }
      if((cmd&0xFF)==0x1D && to!=0){ int ok=syncpt_wait(id,th,to<0? -1 : (i64)to*1000000); if(ok){ u32 v=syncpt_read(id); memcpy(d+12,&v,4); return 0; } }
      u32 slot=ev&63; KEvent* event=nv_event_get(slot);
      nvev_lock(); kevent_clear(event); nvev[slot].id=id; nvev[slot].thresh=th; nvev[slot].armed=1; nvev_unlock(); nv_events_check();
      u32 r=0x10000000u|(slot)|(id<<16); memcpy(d+12,&r,4); return 5; }       /* timeout, caller will wait on the event */
    case 0x1F: { if(n<4) return 1; u32 ev; memcpy(&ev,d,4); nv_event_get(ev); return 0; }
    case 0x20: case 0x21: return 0;
  }
  WARN("ctrl ioctl %x\n",cmd); return 0; }

static u32 ioctl_ctrlgpu(u32 cmd, u8* d, u32 n, u8* extra, u32 nextra){
  (void)n;
  switch(cmd&0xFF){
    case 0x01: { u32 v=0x28000; memcpy(d,&v,4); return 0; }
    case 0x02: { u32 z[10]={0x20,0x20,0x400,0x800,0x20,0x20,0xC0,0x20,0x40,0x10}; memcpy(d,z,40); return 0; }
    case 0x03: case 0x04: return 0;
    case 0x05: { u8 ch[0xA0]; memset(ch,0,sizeof ch); u32* w=(u32*)ch;
      w[0]=0x120; w[1]=0xB; w[2]=0xA1; w[3]=1; u64 l2=0x40000; memcpy(ch+0x10,&l2,8); w[8]=2; w[9]=0x20; w[10]=0x20000; w[11]=0x20000; w[12]=0x1B; w[13]=0x30000; w[14]=1;
      w[15]=0x503; w[16]=0x503; w[17]=0x80; w[18]=0x28; u64 fl=0x55; memcpy(ch+0x50,&fl,8); w[22]=0x902D; w[23]=0xB197; w[24]=0xB1C0; w[25]=0xB06F; w[26]=0xA140; w[27]=0xB0B5;
      w[28]=1; w[29]=0; w[30]=2; w[31]=1; w[32]=0; w[33]=1; w[34]=0x21D70; w[35]=0; u64 cn=0x6230326D67ull; memcpy(ch+0x90,&cn,8);
      u64 sz=0xA0, ad=0xdeadbeef; memcpy(d,&sz,8); memcpy(d+8,&ad,8); memcpy(d+16,ch,0xA0);
      if(extra && nextra) memcpy(extra,ch,nextra<0xA0? nextra : 0xA0);        /* Ioctl3 passes data in the extra buffer */
      return 0; }
    case 0x06: { u32 m=3; memcpy(d+16,&m,4); if(extra && nextra>=4) memcpy(extra,&m,4); return 0; }
    case 0x13: { /* NUM_VSMS: GM20B = 2 SMs (1 GPC x 2 TPC). Ioctl3 variant also returns {gpc,tpc} per SM */
      u32 z[2]={2,0}; memcpy(d,z,8); if(extra && nextra){ u8 mp[4]={0,0,0,1}; memcpy(extra,mp,nextra<4? nextra : 4); } return 0; }
    case 0x14: if((cmd>>30)==3){ /* VSMS_MAPPING: {u64 buf} -> {gpc,tpc} per SM */
        u64 a; memcpy(&a,d,8); u8 mp[4]={0,0,0,1}; if(extra && nextra) memcpy(extra,mp,nextra<4? nextra : 4); else if(a>=0x10000 && a<AS_SIZE-4) memcpy(GP(a),mp,4); return 0; }
      { u32 z[2]={7,1}; memcpy(d,z,8); return 0; }        /* ZBC_GET_ACTIVE_SLOT_MASK */
    case 0x15: { u32 z[2]={7,1}; memcpy(d,z,8); return 0; }
    case 0x1C: { extern u64 guest_ns(void); u64 t=guest_ns(); memcpy(d,&t,8); return 0; }   /* GET_GPU_TIME, same timebase as the system tick */
  }
  WARN("ctrl-gpu ioctl %x\n",cmd); return 0; }

static u32 ioctl_gpu(NvFile* f, u32 cmd, u8* d, u32 n, u8* extra, u32 nextra){
  switch(cmd&0xFF){
    case 0x01: case 0x03: case 0x0B: case 0x0C: case 0x0D: case 0x1D: case 0x0A: case 0x17: return 0;
    case 0x14: memcpy(&f->user_data,d,8); return 0;
    case 0x15: memcpy(d,&f->user_data,8); return 0;
    case 0x16: memset(d,0,n); return 0;
    case 0x09: { u32 cls; memcpy(&cls,d,4); if(f->nclass<8) f->class_ids[f->nclass++]=cls; u64 id=cls; memcpy(d+8,&id,8); return 0; }
    case 0x1A: { if(!f->syncpt) f->syncpt=syncpt_alloc(); u32 fence[2]={f->syncpt,syncpt_read(f->syncpt)}; memcpy(d+24,fence,8); return 0; }   /* the fence goes at +24. at +12 (yuzu's layout) mesa
                                                                                                                                                  reuses its pushbuffers before we read them */
    case 0x08: case 0x1B: {
      u32 num, flags; memcpy(&num,d+8,4); memcpy(&flags,d+12,4);
      u32 fid, fval; memcpy(&fid,d+16,4); memcpy(&fval,d+20,4);
      u64* entries = (cmd&0xFF)==0x08? (u64*)(d+24) : (u64*)extra;
      /* never read entries past what the guest sent */
      if((cmd&0xFF)==0x08 && (u64)num*8>(n>24? n-24 : 0)) num= n>24? (n-24)/8 : 0;
      if((cmd&0xFF)==0x1B && (u64)num*8>nextra) num=nextra/8;
      if(!f->syncpt) f->syncpt=syncpt_alloc();
      u32 incs=((flags&2)? 2 : 0)+((flags&0x100)? fval : 0);
      pm_lock(&g_sync_lk); if((i32)(syncpt_max[f->syncpt]-syncpt[f->syncpt])<0) syncpt_max[f->syncpt]=syncpt[f->syncpt]; syncpt_max[f->syncpt]+=incs; u32 promised=syncpt_max[f->syncpt]; pm_unlock(&g_sync_lk);
      /* the fence to wait for (flag 1) is waited for by the GPU thread before it runs these entries, like on hw.
         waiting here blocked the game thread, and the switch's GBA app increments that fence itself from another
         thread that was waiting for this one: 2 s per frame and a black screen */
      gpu_submit(entries,num,f->syncpt,incs,(flags&1)? fid : ~0u,fval);
      fid=f->syncpt; fval=promised;
      memcpy(d+16,&fid,4); memcpy(d+20,&fval,4);
      return 0; }
  }
  WARN("gpu channel ioctl %x\n",cmd); return 0; }

static int nvdrv_disp(SvcObj* o, IpcCtx* c){
  switch(c->cmd){
    case 0: { u64 a,s; buf_in(c,0,&a,&s); char name[64]={0}; memcpy(name,GP(a),s<63?s:63);
      int dev= !strcmp(name,"/dev/nvmap")? DEV_NVMAP : !strcmp(name,"/dev/nvhost-as-gpu")? DEV_AS : !strcmp(name,"/dev/nvhost-ctrl")? DEV_CTRL :
               !strcmp(name,"/dev/nvhost-ctrl-gpu")? DEV_CTRLGPU : !strcmp(name,"/dev/nvhost-gpu")? DEV_GPU :
               !strcmp(name,"/dev/nvhost-nvdec")? DEV_NVDEC : !strcmp(name,"/dev/nvhost-vic")? DEV_VIC : DEV_OTHER;
      int fd=-1; pm_lock(&g_nvlock); for(int i=1;i<256;i++) if(!nvf[i].dev){ fd=i; memset(&nvf[i],0,sizeof nvf[i]); nvf[i].dev=dev; break; } pm_unlock(&g_nvlock);
      LOG("nvdrv: open %s -> %d\n",name,fd);
      out32(c,(u32)fd); out32(c,0); return 0; }
    case 1: case 11: case 12: {
      u32 fd=in32(c,0), cmd=in32(c,4);
      u64 ia=0,is=0,oa=0,os=0,ea=0,es=0; buf_in(c,0,&ia,&is); buf_out(c,0,&oa,&os);
      if(c->cmd==11){ buf_in(c,1,&ea,&es); }
      if(c->cmd==12){ buf_out(c,1,&ea,&es); }
      u32 size=(cmd>>16)&0x3FFF;
      u64 isz=is, osz=os;
      u32 nb= size? size : (u32)(isz>osz? isz : osz);
      if(fd>=256 || !nvf[fd].dev){ out32(c,8); return 0; }
      /* handlers get the guest's size so the buffer has to hold all of it (a big REMAP batch is bigger than the 16
         KB that fits on the stack) */
      u64 need= isz>osz? isz : osz; if(need<nb) need=nb; if(need>(64u<<20)) need=64u<<20;
      u8 stk[0x4000]; u8* tmp= need>sizeof stk? calloc(1,(size_t)need) : stk; if(!tmp){ out32(c,6); return 0; }   /* NvResult InsufficientMemory */
      if(tmp==stk){ memset(stk,0,sizeof stk); need=sizeof stk; }
      if(ia) memcpy(tmp,GP(ia),isz>need? need : isz);
      if(isz>need) isz=need;
      if(nb>need) nb=(u32)need;
      NvFile* f=&nvf[fd]; u32 r=0;
      { static u32 seen[256]; static int nseen; int k; for(k=0;k<nseen;k++) if(seen[k]==(cmd^((u32)f->dev<<28))) break;
        if(k==nseen && nseen<256){ seen[nseen++]=cmd^((u32)f->dev<<28); LOG("nvdrv: ioctl %08x (disp %d, ipc %u, in %llu out %llu extra %llu)\n",cmd,f->dev,c->cmd,(unsigned long long)is,(unsigned long long)os,(unsigned long long)es); } }
      switch(f->dev){
        case DEV_NVMAP: r=ioctl_nvmap(cmd,tmp,nb); break;
        case DEV_AS: r=ioctl_as(cmd,tmp,(u32)(isz>nb?isz:nb),(c->cmd==12&&ea)? GP(ea):NULL,(u32)es); break;
        case DEV_CTRL: r=ioctl_ctrl(cmd,tmp,nb); break;
        case DEV_CTRLGPU: r=ioctl_ctrlgpu(cmd,tmp,nb,(c->cmd==12&&ea)? GP(ea):NULL,(u32)es); break;
        case DEV_GPU: r=ioctl_gpu(f,cmd,tmp,(u32)(isz>nb?isz:nb),ea? GP(ea):NULL,(u32)es); break;
        case DEV_NVDEC: r=host1x_ioctl(0,cmd,tmp,(u32)(isz>nb?isz:nb)); break;
        case DEV_VIC: r=host1x_ioctl(1,cmd,tmp,(u32)(isz>nb?isz:nb)); break;
        default: WARN("nvdrv: ioctl %x to unknown device\n",cmd); break;
      }
      if(oa) memcpy(GP(oa),tmp,osz>need? need : osz);
      if(tmp!=stk) free(tmp);
      out32(c,r); return 0; }
    case 2: { u32 fd=in32(c,0); if(fd<256) nvf[fd].dev=0; out32(c,0); return 0; }
    case 3: out32(c,0); return 0;
    case 4: { u32 fd=in32(c,0), id=in32(c,4); (void)fd;
      u32 slot=id&63; if(((id>>28)&0xF)==1 || id<64){ KEvent* event=nv_event_get(slot); out_copy_h(c,h_new(&event->o)); }
      else { KEvent* e=kevent_new("nv query",0); out_copy_h(c,h_new(&e->o)); }
      out32(c,0); return 0; }
    case 5: case 6: case 8: case 9: case 10: case 13: out32(c,0); return 0;
  }
  return stub_ok(o,c); }

static SvcObj* new_nvdrv(void){ return svcobj_new("nvdrv",nvdrv_disp,0); }

void nv_init(void){
  pm_init(&g_nvlock); pm_init(&g_sync_lk); pc_init(&g_sync_cv);
  gmmu=calloc(GL1,sizeof(u64*));
  svc_register("nvdrv",new_nvdrv); svc_register("nvdrv:a",new_nvdrv); svc_register("nvdrv:s",new_nvdrv); svc_register("nvdrv:t",new_nvdrv);
}
