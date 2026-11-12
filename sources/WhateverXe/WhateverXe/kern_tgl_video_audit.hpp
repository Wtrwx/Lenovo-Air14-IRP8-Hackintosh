// Opt-in CPU command receipts, not GPU completion or allocation evidence.
// Byte snapshots are bounded prefixes. Hashes are FNV-1a-64, not SHA256.
#ifndef KERN_TGL_VIDEO_AUDIT_HPP
#define KERN_TGL_VIDEO_AUDIT_HPP
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kern_tgl_video_progressive.hpp"
#include "kern_tgl_video_alias.hpp"
#include "kern_tgl_video_map.hpp"
namespace TGLVideoAudit {
constexpr unsigned PrefixBytes=4096, Recent=64, Early=16;
enum Field {Schema,BeginNS,EndNS,Ring,Context,ContextID,GPU,Stamp,Engine,Words,
    CopiedBefore,CopiedAfter,HashBefore,HashAfter,Disposition,PlanReason,Edits,
    Slices,OwnerGuard,Category,ParseStop,Flags,SurfaceReason,SurfaceDisposition,Fields};
enum Action {ObserveOnly,PlanRejected,Applied,ChangedBeforeApply,ReadFailed,MappingRejected};
struct Record {
    uint64_t q[Fields]{};
    TGLVideoProgressive::Edit edits[33]{};
    uint8_t before[PrefixBytes]{};
    TGLVideoAlias::Receipt alias{};
    TGLVideoMap::Receipt map{};
};
static_assert(offsetof(Record,alias)==4688 && offsetof(Record,map)==6864 && sizeof(Record)==7456,"version 4 submission receipt");
inline uint64_t hash(const uint8_t *bytes,size_t n,uint64_t h=14695981039346656037ULL) {
    for(size_t i=0;i<n;++i){h^=bytes[i];h*=1099511628211ULL;}return h;
}
// Only structural commands contribute category hints. No payload-word search.
// Unknown/truncated packets stop classification; no address is dereferenced.
inline unsigned category(const uint8_t *bytes,unsigned n,unsigned &stop) {
    unsigned flags=0;stop=0;
    while(stop+4<=n) {
        uint32_t h;memcpy(&h,bytes+stop,4);unsigned dw=0;
        if(h==0x13000083 || h==0x13000003 || h==0x10000003)dw=5;
        else if(h==0x12000002)dw=4;
        else if((h>>29)==3)dw=((h>>27)&3)==1?1:(h&4095)+2;
        else if((h>>29)==0) {
            const unsigned op=(h>>23)&63;
            if(op==0 || op==5 || op==10)dw=1;
            else if(op==0x1c || op==0x20 || op==0x22 || op==0x24 || op==0x31)dw=(h&255)+2;
        }
        if(!dw || dw>(n-stop)/4)break;
        if(h==0x70000003)flags|=1;
        // Both observed short and Gen12 six-DWORD framing are category hints.
        // This does not admit the extended state to either command-edit plan.
        if(h==0x70800000 || h==0x70800004)flags|=2;
        if(h==0x7103000f)flags|=4;
        stop+=dw*4;
    }
    return flags;
}
// Writers/readers never wait for a slot. Collisions and snapshot holes are
// explicit. The two banks are sampled sequentially, not atomically together.
template<unsigned Capacity=Recent,unsigned First=Early> struct History {
    static_assert(Capacity>0 && First>0,"nonempty receipt banks");
    struct Slot {uint32_t busy{};uint64_t sequence{};Record record{};};
    Slot recent[Capacity]{},early[First]{};
    uint64_t attempts{},drops{};
    uint32_t frozen{};
    bool stopped() const {return __atomic_load_n(&frozen,__ATOMIC_ACQUIRE)!=0;}
    void freeze(){__atomic_store_n(&frozen,1U,__ATOMIC_RELEASE);}
    bool put(Slot &s,uint64_t sequence,const Record &record) {
        uint32_t zero=0;
        if(!__atomic_compare_exchange_n(&s.busy,&zero,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return false;
        const bool ok=!stopped() && sequence>s.sequence;if(ok){s.record=record;s.sequence=sequence;}
        __atomic_store_n(&s.busy,0U,__ATOMIC_RELEASE);return ok;
    }
    void append(const Record &record) {
        if(stopped())return;
        const auto n=__atomic_fetch_add(&attempts,1ULL,__ATOMIC_RELAXED);
        if(!put(recent[n%Capacity],n+1,record))__atomic_fetch_add(&drops,1ULL,__ATOMIC_RELAXED);
        if(n<First && !put(early[n],n+1,record))__atomic_fetch_add(&drops,1ULL,__ATOMIC_RELAXED);
    }
    static uint64_t copy(Slot &s,Record &out) {
        uint32_t zero=0;
        if(!__atomic_compare_exchange_n(&s.busy,&zero,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
        const auto sequence=s.sequence;if(sequence)out=s.record;
        __atomic_store_n(&s.busy,0U,__ATOMIC_RELEASE);return sequence;
    }
};
}
#endif
