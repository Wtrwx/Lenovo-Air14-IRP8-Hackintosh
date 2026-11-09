#include "kern_display13_association_evidence.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace Display13AssociationEvidence;
template<class T>void put(void *p,size_t off,T v){memcpy(static_cast<uint8_t *>(p)+off,&v,sizeof(v));}
struct Fixture {
    uint8_t controller[0x1C20]{},fb[0x4B90]{};
    Fixture(){put(fb,0x1D0,reinterpret_cast<uintptr_t>(controller));
        put(controller,0xD5C,uint32_t(2));put(fb,0x1DC,uint32_t(1));
        put(controller,0xD68,reinterpret_cast<uintptr_t>(fb));}
    Report report(uintptr_t owner=0,bool native=false,uintptr_t logical=0){
        const auto r=inspect(controller,sizeof(controller),fb,sizeof(fb),owner,native,logical);
        assert((r.failures==0)==Display13FBMemory::associations(controller,sizeof(controller),fb,sizeof(fb),owner,native,logical));
        return r;
    }
};
int main(){
    Fixture f;assert(f.report().failures==0);
    assert(inspect(nullptr,0,nullptr,0).failures==(ControllerMissing|FramebufferMissing|ControllerSize|FramebufferSize));
    assert(inspect(f.controller,0,f.fb,sizeof(f.fb)).failures==ControllerSize);
    assert(inspect(f.controller,sizeof(f.controller),f.fb,0).failures==FramebufferSize);
    const auto mutation=[](bool ctl,size_t off,uint64_t value,unsigned bytes,uint64_t expected){
        Fixture q;void *p=ctl?static_cast<void *>(q.controller):q.fb;
        memcpy(static_cast<uint8_t *>(p)+off,&value,bytes);assert(q.report().failures==expected);
    };
    mutation(false,0x1D0,0,8,ControllerLink);mutation(true,0xC0,42,8,Owner);
    mutation(true,0xE49,1,1,Transition);mutation(false,0x4340,42,8,LogicalSurface);
    mutation(false,0x4348,42,8,NativeSurface);mutation(true,0xD5C,0,4,Count|Index);
    mutation(true,0xD5C,4,4,Count);mutation(false,0x1DC,2,4,Index);
    mutation(true,0xD68,0,8,ArrayLink);
    assert(f.report(0,false,42).failures==(LogicalSurface|LogicalOwner));
    // Enumerate independent donor states; malformed indices never read an
    // out-of-bounds array entry, even when several earlier guards also fail.
    unsigned combinations=0;
    for(unsigned count=0;count<6;++count)for(unsigned index=0;index<7;++index)
    for(unsigned owner=0;owner<2;++owner)for(unsigned surface=0;surface<2;++surface)
    for(unsigned native=0;native<2;++native)for(unsigned permitted=0;permitted<2;++permitted){
        put(f.controller,0xD5C,count);put(f.fb,0x1DC,index);
        put(f.controller,0xC0,uintptr_t(owner*42));put(f.fb,0x4340,uintptr_t(surface*84));
        put(f.fb,0x4348,uintptr_t(native*126));
        for(uintptr_t expected:{uintptr_t(0),uintptr_t(42)})
        for(uintptr_t logical:{uintptr_t(0),uintptr_t(84)}){f.report(expected,permitted,logical);++combinations;}
    }
    put(f.fb,0x1DC,UINT32_MAX);assert(f.report().failures&Index);
    printf("association evidence: each rejection isolated; %u contract parity cases passed\n",combinations);
}
