// CPU-only diagnostics for the exact guarded native association contract.
// This report never authorizes ownership, teardown or register access.
#ifndef KERN_DISPLAY13_ASSOCIATION_EVIDENCE_HPP
#define KERN_DISPLAY13_ASSOCIATION_EVIDENCE_HPP
#include "kern_display13_fb_memory.hpp"
namespace Display13AssociationEvidence {
enum Failure : uint64_t {
    ControllerMissing=1ULL<<0, FramebufferMissing=1ULL<<1,
    ControllerSize=1ULL<<2, FramebufferSize=1ULL<<3,
    ControllerLink=1ULL<<4, Owner=1ULL<<5, Transition=1ULL<<6,
    LogicalSurface=1ULL<<7, LogicalOwner=1ULL<<8, NativeSurface=1ULL<<9,
    Count=1ULL<<10, Index=1ULL<<11, ArrayLink=1ULL<<12
};
struct Report {
    uint64_t failures {},owner {},surface {},nativeSurface {},transition {},
        controllerLink {},index {},count {},arrayLink {};
};
// Caller supplies class-checked objects and holds the native FB mutex and
// display owner lock. Do not follow pointers from the observed private fields.
inline Report inspect(const void *controller,size_t controllerBytes,const void *fb,size_t fbBytes,
                      uintptr_t expectedOwner=0,bool nativeSurface=false,uintptr_t logicalSurface=0) {
    Report r;
    if(!controller)r.failures|=ControllerMissing;
    if(!fb)r.failures|=FramebufferMissing;
    if(controllerBytes<0x1C20)r.failures|=ControllerSize;
    if(fbBytes<0x4B90)r.failures|=FramebufferSize;
    if(r.failures)return r;
    using Display13FBMemory::field;
    r.owner=field<uintptr_t>(controller,0xC0);r.surface=field<uintptr_t>(fb,0x4340);
    r.nativeSurface=field<uintptr_t>(fb,0x4348);r.transition=field<uint8_t>(controller,0xE49);
    r.controllerLink=field<uintptr_t>(fb,0x1D0);
    r.index=field<uint32_t>(fb,0x1DC);r.count=field<uint32_t>(controller,0xD5C);
    if(r.controllerLink!=reinterpret_cast<uintptr_t>(controller))r.failures|=ControllerLink;
    if(r.owner!=expectedOwner)r.failures|=Owner;
    if(r.transition)r.failures|=Transition;
    if(r.surface!=logicalSurface)r.failures|=LogicalSurface;
    if(logicalSurface && !expectedOwner)r.failures|=LogicalOwner;
    if(r.nativeSurface && (!nativeSurface || !expectedOwner))r.failures|=NativeSurface;
    if(!r.count || r.count>3)r.failures|=Count;
    if(r.index>=r.count)r.failures|=Index;
    // Invalid count/index never selects an arbitrary private-array address.
    if(!(r.failures&(Count|Index))) {
        r.arrayLink=field<uintptr_t>(controller,0xD60+8*r.index);
        if(r.arrayLink!=reinterpret_cast<uintptr_t>(fb))r.failures|=ArrayLink;
    }
    return r;
}
}
#endif
