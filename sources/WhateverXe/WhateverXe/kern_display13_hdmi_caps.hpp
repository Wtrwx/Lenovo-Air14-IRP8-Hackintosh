// CTA HDMI/HDMI Forum VSDB link limits, following Linux v6.12 drm_edid.c
// and intel_hdmi.c. No monitor identity or fabricated timing is involved.
#ifndef KERN_DISPLAY13_HDMI_CAPS_HPP
#define KERN_DISPLAY13_HDMI_CAPS_HPP
#include <stdint.h>
#include <stddef.h>
namespace Display13HDMI {
struct Caps {
    bool valid {},hdmi {},scdc {};uint32_t maxKHz {};
    uint32_t legacyKHz {},forumKHz {};bool forumPresent {};
};
inline bool checksum(const uint8_t *b,size_t n) {
    if(!b || n!=128)return false;
    unsigned sum=0;for(unsigned i=0;i<128;++i)sum+=b[i];return !(sum&255);
}
inline void resolve(Caps &c) {
    c.maxKHz=c.forumKHz ? c.forumKHz : c.legacyKHz ? c.legacyKHz : 165000;
    if(c.maxKHz>340000 && !c.scdc)c.maxKHz=340000;
    if(c.maxKHz>600000)c.maxKHz=600000;
}
inline Caps extension(const uint8_t *b,size_t n) {
    Caps c;if(!checksum(b,n) || b[0]!=2 || b[1]<3 || b[2]<4 || b[2]>127)return c;
    uint32_t legacy=0,forum=0;
    for(unsigned pos=4;pos<b[2];) {
        const unsigned length=b[pos]&31,tag=b[pos]>>5;
        if(pos+1+length>b[2])return {};
        const auto p=b+pos+1;
        if(tag==3 && length>=3) {
            const uint32_t oui=p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);
            if(oui==0x000C03) {c.hdmi=true;if(length>=7)legacy=uint32_t(p[6])*5000;}
            if(oui==0xC45DD8 && length>=6) {c.hdmi=true;c.forumPresent=true;forum=uint32_t(p[4])*5000;c.scdc=p[5]&0x80;}
        }
        pos+=1+length;
    }
    c.valid=true;
    c.legacyKHz=legacy;c.forumKHz=forum;
    // Rates above HDMI 1.4 require the sink's scrambling/SCDC handshake.
    resolve(c);
    return c;
}
inline Caps combine(Caps a,const Caps &b) {
    if(!b.valid)return a;
    if(!a.valid)return b;
    a.hdmi=a.hdmi || b.hdmi;
    if(b.legacyKHz>a.legacyKHz)a.legacyKHz=b.legacyKHz;
    if(b.forumPresent) {
        // Conflicting duplicate Forum blocks must not inflate the link rate.
        a.scdc=a.forumPresent ? a.scdc && b.scdc : b.scdc;
        if(b.forumKHz && (!a.forumKHz || b.forumKHz<a.forumKHz))a.forumKHz=b.forumKHz;
        a.forumPresent=true;
    }
    resolve(a);return a;
}
// The exact donor accepts EDID block numbers 1..8. Keep its native bound;
// retain each extension separately so a reread replaces its old capability.
struct EDIDCache {
    static constexpr unsigned Extensions=7;
    bool baseValid {};uint8_t advertised {},seen {};Caps blocks[Extensions] {};
    void reset(){baseValid=false;advertised=seen=0;for(auto &b:blocks)b={};}
    bool observe(unsigned block,const uint8_t *data,bool success) {
        if(block==1) {
            reset();
            static constexpr uint8_t header[]={0,255,255,255,255,255,255,0};
            if(!success || !checksum(data,128))return true;
            for(unsigned i=0;i<8;++i)if(data[i]!=header[i])return true;
            baseValid=true;advertised=data[126];return true;
        }
        if(!baseValid || block<2 || block>Extensions+1 || block>unsigned(advertised)+1)return false;
        blocks[block-2]=success ? extension(data,128) : Caps {};
        seen|=uint8_t(1U<<(block-2));return true;
    }
    bool missing(unsigned block)const {
        return baseValid && block>=2 && block<=Extensions+1 && block<=unsigned(advertised)+1 &&
            !(seen&(1U<<(block-2)));
    }
    Caps caps()const {
        Caps result;
        for(const auto &b:blocks)result=combine(result,b);
        return result;
    }
};
inline uint64_t sourceLimitHz(const Caps &c,uint64_t original,bool sourceSCDC=false) {
    // Raise only the exact donor's HDMI source cap. Native sink validation,
    // disconnect failures and DP/eDP timing ranges are left to their owners.
    // Only the installed SCDC adapter may expose HDMI 2.0 rates. A sink
    // capability alone cannot certify the source's scrambling/ratio path.
    const auto supported=c.maxKHz>340000 && !(sourceSCDC && c.scdc) ? 340000 : c.maxKHz;
    return c.valid && c.hdmi && original==300000000 && supported>300000 ?
        uint64_t(supported)*1000 : original;
}
inline uint16_t sdrDepth(uint16_t encoding,uint16_t depth,uint16_t range) {
    // This exact donor calculates HDMI PLLs from pixel rate without the
    // 10/12/16-bpc TMDS multiplier. Use its implemented 8-bpc path for SDR
    // RGB/444; preserve subsampling, HDR and unknown/multibit selections.
    return (encoding==1 || encoding==2) && range==1 &&
        (depth==4 || depth==8 || depth==16) ? 2 : depth;
}
struct GCPResult {bool eligible {},complete {};uint32_t depth {},control {},gcp {},writes {};};
// Linux intel_hdmi_compute_gcp_infoframe / hsw_set_infoframes: HDMI deep
// colour needs GCP_COLOR_INDICATION and VIDEO_DIP_ENABLE_GCP_HSW. The hardware
// derives the packet's colour-depth field from the active transcoder BPC.
template<class IO> GCPResult ensureGCP(IO &io,unsigned pipe) {
    GCPResult r;if(pipe>=3)return r;
    const uint32_t bank=0x1000*pipe;
    const auto func=io.read(0x60400+bank);
    r.control=io.read(0x60200+bank);
    if(func==UINT32_MAX || r.control==UINT32_MAX || !(func&(1U<<31)) ||
       (func&(7U<<24)) || !(r.control&(1U<<12)))return r;
    const auto code=(func>>20)&7;
    if(code!=0 && code!=1 && code!=3)return r;
    r.eligible=true;r.depth=code==0?8:code==1?10:12;
    // Do not assert a default packing phase without timing-alignment proof.
    // Clear stale mute/reserved/phase bits inherited from firmware.
    r.gcp=r.depth>8 ? 4 : 0;
    io.write(0x60210+bank,r.gcp);++r.writes;
    if(io.read(0x60210+bank)!=r.gcp)return r;
    io.write(0x60200+bank,r.control|(1U<<16));++r.writes;
    r.complete=io.read(0x60200+bank)==(r.control|(1U<<16));
    return r;
}
}
#endif
