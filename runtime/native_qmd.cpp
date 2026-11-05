/* SwitchRecomp, AGPL-3.0; uses the retained GPL-3.0-or-later Eden scheduler. */
#include "native_qmd.h"
#include "video_core/native_qmd.h"
#include <cstring>
#include <cstdio>
#include <exception>
#include <new>
#include <vector>
#include <unordered_map>

struct SwrNativeQmd {
    struct Lease { uint64_t address,size; uint32_t token; };
    SwrNativeQmdBackend backend;
    void* owner;
    std::mutex mutex;
    Tegra::NativeQmdScheduler scheduler;
    std::unordered_map<uint64_t,std::vector<Lease>> transactions;
    uint64_t current{};
    size_t leases{};
    const char* detail{};
    explicit SwrNativeQmd(const SwrNativeQmdBackend& b,void* o):backend(b),owner(o) {
        scheduler.trace=[this](std::string_view event,uint64_t address,int32_t){
            current=address;
            if(event=="complete") retire(address);
        };
    }
    void retire(uint64_t address) {
        auto found=transactions.find(address); if(found==transactions.end()) return;
        for(const auto& lease:found->second){ backend.release(owner,lease.token); --leases; }
        transactions.erase(found);
    }
    ~SwrNativeQmd(){ while(!transactions.empty()) retire(transactions.begin()->first); }
    bool hold(uint64_t address,uint64_t size) {
        constexpr uint64_t Limit=uint64_t{1}<<49;
        if(!size || address>=Limit || size>Limit-address){ detail="invalid native GPU span"; return false; }
        const uint64_t first=address&~uint64_t{0xfff};
        const uint64_t bytes=((address-first)+size+0xfff)&~uint64_t{0xfff};
        if(transactions.size()>=4096 && !transactions.contains(current)){ detail="QMD transaction limit"; return false; }
        auto& owned=transactions[current];
        for(const auto& lease:owned)
            if(first>=lease.address && first-lease.address<=lease.size && bytes<=lease.size-(first-lease.address)) return true;
        if(leases>=4096){ detail="QMD lease limit"; return false; }
        /* Allocate vector capacity before borrowing a resource, so an allocation
           exception cannot strand a native memory pin. */
        owned.reserve(owned.size()+1);
        uint32_t token{};
        if(!backend.acquire(owner,first,bytes,&token)){ detail="unowned or unmapped native GPU span"; return false; }
        owned.push_back({first,bytes,token}); ++leases; return true;
    }
    static int read(void* context,uint64_t address,void* dest,uint32_t size) {
        auto& self=*static_cast<SwrNativeQmd*>(context);
        return self.hold(address,size) && self.backend.read(self.owner,address,dest,size);
    }
    bool descriptor(uint64_t address,Tegra::NativeQmdScheduler::Descriptor& qmd) {
        current=address;
        if(address&0xff){ detail="unaligned QMD3 address"; return false; }
        if(!read(this,address,qmd.data(),sizeof qmd)) return false;
        /* Pin all release destinations before executing any shader. Never
           truncate an unreviewed wider release-address encoding to 40 bits. */
        for(unsigned slot:{24u,28u,52u}){
            const uint32_t flags=qmd[slot+1]; if(!(flags&(1u<<23))) continue;
            if(flags&0x1ff00u){ detail="unsupported wide QMD release address"; return false; }
            const uint64_t target=(uint64_t{flags&0xffu}<<32)|qmd[slot];
            if((target&3) || !hold(target,4)) return false;
        }
        return true;
    }
    bool dispatch(const Tegra::NativeQmdScheduler::Descriptor& qmd) {
        auto decoded=Tegra::DecodeNativeComputeLaunch(qmd);
        if(!decoded){ detail="unsupported QMD3 launch layout"; return false; }
        SwrNativeLaunch launch{}; launch.program=decoded->program_address;
        launch.shared_bytes=decoded->shared_bytes;
        launch.local_pos_bytes=decoded->local_pos_bytes; launch.local_crs_bytes=decoded->local_crs_bytes;
        launch.linked_tsc=decoded->linked_tsc;
        if(!hold(launch.program,16)) return false;
        for(unsigned i=0;i<3;i++){ launch.grid[i]=decoded->grid[i]; launch.block[i]=decoded->block[i]; }
        for(unsigned i=0;i<8;i++){
            const auto& cb=decoded->constant_buffers[i];
            launch.cbuf[i]={cb.gpu_address,cb.size,cb.enabled};
            if(cb.enabled && cb.size && !hold(cb.gpu_address,cb.size)) return false;
        }
        if(!backend.dispatch(owner,&launch,read,this)){ detail="native dispatch rejected or incomplete"; return false; }
        return true;
    }
};

extern "C" SwrNativeQmd* swr_native_qmd_create(const SwrNativeQmdBackend* backend,void* owner) {
    if(!backend || !owner || !backend->acquire || !backend->release || !backend->read ||
       !backend->write_release || !backend->dispatch) return nullptr;
    try { return new SwrNativeQmd(*backend,owner); } catch(...) { return nullptr; }
}
extern "C" void swr_native_qmd_destroy(SwrNativeQmd* session){ delete session; }
extern "C" int swr_native_qmd_signal(SwrNativeQmd* session,uint64_t address,uint32_t action,
                                    char* error,uint32_t size) {
    const auto fail=[&](const char* message){ if(error && size) std::snprintf(error,size,"%s",message); return 0; };
    if(error && size) error[0]=0;
    if(!session || (address&0xff) || address>=(uint64_t{1}<<49)) return fail("invalid QMD3 session/address");
    try {
        std::lock_guard lock(session->mutex); session->current=address; session->detail=nullptr;
        auto result=session->scheduler.Signal(address,action,
            [&](uint64_t at,auto& qmd){ return session->descriptor(at,qmd); },
            [&](uint64_t at,uint32_t payload,uint32_t flags){ return session->backend.write_release(session->owner,at,payload,flags)!=0; },
            [&](const auto& qmd){ return session->dispatch(qmd); });
        if(!result.empty()) return fail(session->detail?session->detail:result.data());
        return 1;
    } catch(const std::exception& e) { return fail(e.what()); }
    catch(...) { return fail("QMD3 execution exception"); }
}
