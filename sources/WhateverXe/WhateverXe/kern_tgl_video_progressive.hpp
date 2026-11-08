// Experimental, opt-in correction of one contradictory progressive AVC state.
// Packet fields: Intel media-driver Gen12 mhw_vdbox_mfx_hwcmd_g12_X.h.
// This is not a general command parser and does not admit decoder batches.
#ifndef KERN_TGL_VIDEO_PROGRESSIVE_HPP
#define KERN_TGL_VIDEO_PROGRESSIVE_HPP
#include <stdint.h>
#include <stddef.h>
namespace TGLVideoProgressive {
struct Edit {uint32_t index, before, after;};
struct Plan {Edit edits[33] {}; uint32_t count {}, slices {};};
inline bool plan(const uint32_t *w, size_t count, Plan &out) {
    out={};
    if(!w || count<21 || count>262144)return false;
    uint32_t image=UINT32_MAX,width=0,height=0,priorEnd=0;
    uint32_t slice=UINT32_MAX,walkers=0;
    bool vdenc=false,encoder=false,last=false;
    for(size_t i=0;i<count;) {
        const uint32_t h=w[i];size_t n=0;
        if((h>>29)==3)n=(h&0xfff)+2;
        else if(h==0)n=1;
        else if(h==0x13000083 || h==0x13000003 || h==0x10000003)n=5;
        else if(h==0x12000002)n=4;
        else return false;
        if(n>count-i)return false;
        if(h==0x70000003) {
            // AVC standard (2), encoder CodecSelect bit 4. One pipe selection.
            if(encoder || (w[i+1]&0x1f)!=0x12)return false;
            encoder=true;
        } else if(h==0x70800000) {
            if(vdenc || w[i+1]!=0x82)return false;
            vdenc=true;
        } else if(h==0x71000013) {
            if(image!=UINT32_MAX || !encoder || !vdenc)return false;
            image=uint32_t(i);
            width=(w[i+2]&0xff)+1;height=((w[i+2]>>16)&0xff)+1;
            // frame_mbs_only=1 forbids MBAFF. Field pictures are excluded.
            if((w[i+4]&7)!=6 || (w[i+1]&0xffff)!=width*height ||
                (w[i+3]&0x300))return false;
        } else if(h==0x71030009) {
            if(image==UINT32_MAX || slice!=UINT32_MAX || last || walkers>=16)return false;
            slice=uint32_t(i);
        } else if(h==0x70870002) {
            if(slice==UINT32_MAX || w[i+3]!=0)return false;
            const auto start=w[i+1],end=w[i+2];
            if(start!=priorEnd || end<=start || end>height)return false;
            last=(w[slice+6]&(1U<<19))!=0;
            if(last!=(end==height))return false;
            // Require the complete observed factor-of-two contradiction.
            const uint32_t badStart=((start*2)<<24)|((start*2*width)&0x7fff);
            const uint32_t badEnd=(last?height:end*2)<<16;
            if(w[slice+4]!=badStart || w[slice+5]!=badEnd)return false;
            out.edits[out.count++]={slice+4,w[slice+4],(start<<24)|(start*width)};
            out.edits[out.count++]={slice+5,w[slice+5],end<<16};
            ++walkers;priorEnd=end;slice=UINT32_MAX;
        }
        i+=n;
    }
    if(image==UINT32_MAX || !last || priorEnd!=height || slice!=UINT32_MAX || walkers<2)return false;
    out.edits[out.count++]={image+4,w[image+4],w[image+4]&~2U};
    out.slices=walkers;return true;
}
// Check every old value first, so a changed buffer is never partially rewritten.
inline bool apply(uint32_t *w,size_t count,const Plan &p) {
    if(!w || !p.count || p.count>33)return false;
    for(unsigned i=0;i<p.count;++i)if(p.edits[i].index>=count || w[p.edits[i].index]!=p.edits[i].before)return false;
    for(unsigned i=0;i<p.count;++i)w[p.edits[i].index]=p.edits[i].after;
    return true;
}
}
#endif
