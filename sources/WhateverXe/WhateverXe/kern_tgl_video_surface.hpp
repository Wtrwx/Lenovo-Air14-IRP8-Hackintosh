// Experimental CPU-only planner for the AVC VDEnc NV12 format contradiction.
// Intel media-driver 4f4a07a39f1731ec89838c323df2bc3c0a026fab:
// media_driver/agnostic/gen12/hw/vdbox/mhw_vdbox_vdenc_hwcmd_g12_X.h
// VDENC_Surface_State_Fields_CMD.DW1.SurfaceFormat is bits 27:31; NV12 is 4.
// MFX SurfaceFormat instead occupies bits 28:31, with interleave in bit 27.
// Parsing these states does not establish allocation ownership or completion.
#ifndef KERN_TGL_VIDEO_SURFACE_HPP
#define KERN_TGL_VIDEO_SURFACE_HPP
#include "kern_tgl_video_progressive.hpp"
namespace TGLVideoSurface {
using Edit=TGLVideoProgressive::Edit;
struct Plan {Edit edits[3]{};uint32_t count{},slices{};};
enum class Reason : uint32_t {Ready,Bounds,Packet,Truncated,Pipe,Mode,SurfaceOrder,
    SurfaceID,Duplicate,Image,Slice,Walker,Incomplete,Layout,Pair,Geometry};
inline bool plan(const uint32_t *w,size_t count,Plan &out,Reason *reason=nullptr,
                 TGLVideoMode::Layout layout=TGLVideoMode::Layout::ObservedShort) {
    auto reject=[&](Reason r){if(reason)*reason=r;return false;};
    out={};if(reason)*reason=Reason::Ready;
    if(!w || count<21 || count>262144)return reject(Reason::Bounds);
    uint32_t mfx[3]{UINT32_MAX,UINT32_MAX,UINT32_MAX};
    uint32_t vd[3]{UINT32_MAX,UINT32_MAX,UINT32_MAX};
    uint32_t image=UINT32_MAX,slice=UINT32_MAX,width=0,height=0,end=0,walkers=0;
    bool encoder=false,vdenc=false,last=false;
    for(size_t i=0;i<count;) {
        const uint32_t h=w[i];size_t n=0;
        if((h>>29)==3)n=(h&4095)+2;
        else if(h==0)n=1;
        else if(h==0x13000083 || h==0x13000003 || h==0x10000003)n=5;
        else if(h==0x12000002)n=4;
        else return reject(Reason::Packet);
        if(n>count-i)return reject(Reason::Truncated);
        if(h==0x70000003) {
            if(encoder || image!=UINT32_MAX || (w[i+1]&31)!=0x12)return reject(Reason::Pipe);
            encoder=true;
        } else if(h==0x70800000 || (layout==TGLVideoMode::Layout::ReferenceAVCNV12SixDWORD && h==0x70800004)) {
            if(vdenc || image!=UINT32_MAX || !TGLVideoMode::matches(w+i,count-i,layout))return reject(Reason::Mode);
            vdenc=true;
        } else if(h==0x70010004 || h==0x70810004 || h==0x70820004 || h==0x70830004) {
            if(!encoder || image!=UINT32_MAX || (h!=0x70010004 && !vdenc))return reject(Reason::SurfaceOrder);
            uint32_t slot=0;uint32_t *states=vd;
            if(h==0x70010004) {
                states=mfx;slot=w[i+1]==4?0:w[i+1]==0?1:w[i+1]==5?2:3;
                if(slot==3)return reject(Reason::SurfaceID);
            } else {
                slot=(h>>16)-0x7081;
                if(w[i+1]!=0)return reject(Reason::SurfaceID);
            }
            if(states[slot]!=UINT32_MAX)return reject(Reason::Duplicate);
            states[slot]=uint32_t(i);
        } else if(h==0x71000013) {
            if(!encoder || !vdenc || image!=UINT32_MAX)return reject(Reason::Image);
            image=uint32_t(i);width=(w[i+2]&255)+1;height=((w[i+2]>>16)&255)+1;
            // Require already coherent progressive geometry; never alter it here.
            if((w[i+4]&7)!=4 || (w[i+3]&0x300) || (w[i+1]&65535)!=width*height)
                return reject(Reason::Image);
        } else if(h==0x71030009) {
            if(image==UINT32_MAX || slice!=UINT32_MAX || last || walkers>=16)return reject(Reason::Slice);
            slice=uint32_t(i);
        } else if(h==0x70870002) {
            if(slice==UINT32_MAX || w[i+3] || w[i+1]!=end || w[i+2]<=end || w[i+2]>height)
                return reject(Reason::Walker);
            const auto start=end;end=w[i+2];last=(w[slice+6]&(1U<<19))!=0;
            if(last!=(end==height) || w[slice+4]!=((start<<24)|(start*width)) || w[slice+5]!=(end<<16))
                return reject(Reason::Geometry);
            ++walkers;slice=UINT32_MAX;
        }
        i+=n;
    }
    if(image==UINT32_MAX || slice!=UINT32_MAX || !last || end!=height || !walkers)
        return reject(Reason::Incomplete);
    for(unsigned s=0;s<3;++s) {
        if(mfx[s]==UINT32_MAX || vd[s]==UINT32_MAX)return reject(Reason::Incomplete);
        const auto a=mfx[s],b=vd[s];
        for(unsigned j=2;j<6;++j)if(w[a+j]!=w[b+j])return reject(Reason::Pair);
        const auto size=w[b+2],layout=w[b+3],uv=w[b+4];
        const uint32_t x=((size>>4)&16383)+1,y=(size>>18)+1,pitch=((layout>>3)&131071)+1;
        // Restrict to the observed Y-tiled, full chroma pitch, no other controls.
        // Exclude already corrected states and all other pixel format patterns.
        if((size&15) || (layout&~0x000ffff8U)!=0x48000003U ||
           pitch<x || (pitch&127) || uv!=w[b+5] || uv<y || uv>((y+31)&~31U))
            return reject(Reason::Layout);
        if(s<2 ? (x!=width*16 || y!=height*16) : (x!=width*4 || y!=height*4))
            return reject(Reason::Geometry);
    }
    // Publish a complete plan only after every paired surface has passed.
    for(unsigned s=0;s<3;++s) {
        const auto i=vd[s]+3;out.edits[s]={i,w[i],(w[i]&0x07ffffffU)|(4U<<27)};
    }
    out.count=3;out.slices=walkers;return true;
}
inline bool apply(uint32_t *w,size_t count,const Plan &p) {
    if(!w || p.count!=3)return false;
    for(unsigned i=0;i<3;++i) {
        if(p.edits[i].index>=count || w[p.edits[i].index]!=p.edits[i].before ||
           (p.edits[i].before&~0x000ffff8U)!=0x48000003U ||
           p.edits[i].after!=((p.edits[i].before&0x07ffffffU)|(4U<<27)))return false;
        for(unsigned j=0;j<i;++j)if(p.edits[i].index==p.edits[j].index)return false;
    }
    for(unsigned i=0;i<3;++i)w[p.edits[i].index]=p.edits[i].after;
    return true;
}
}
#endif
