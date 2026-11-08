// HDMI 2.0 sink negotiation. Register/bit definitions follow Linux v6.12
// drm_scdc_helper.c and i915_reg.h. The caller owns DDC and the disabled
// transcoder; it must apply sourceFlags only when complete is true.
#ifndef KERN_DISPLAY13_HDMI_SCDC_HPP
#define KERN_DISPLAY13_HDMI_SCDC_HPP
#include <stdint.h>
namespace Display13HDMI {
struct LinkPlan {bool valid {},scdc {};uint64_t maximumHz {};};
inline LinkPlan linkPlan(uint32_t control,uint16_t encoding,uint16_t depth,
                         uint64_t sinkMaxHz,bool sinkKnown,bool scdc) {
    const auto mode=(control>>24)&7;
    if(mode>1 || (control&(7U<<20)) || depth!=2 ||
       (encoding!=1 && encoding!=2) || (mode==1 && encoding!=1))return {};
    // Native DVI uses the same physical TMDS connector without HDMI packets
    // or SCDC. Keep single-link RGB operation for legacy displays/adapters.
    if(mode==1)return {true,false,165000000};
    return {true,sinkKnown && scdc,sinkKnown?sinkMaxHz:300000000};
}
template<class Request> void scdcReadRequest(Request &r,uint8_t (&bytes)[2],uint8_t &value) {
    // SCDC uses a register-pointer write followed by a repeated-start read.
    // Keep both caller-owned buffers alive through the native command gate;
    // do not depend on GMBUS's indexed-read cycle.
    r.sendTransactionType=1;r.sendAddress=0xA8;
    r.sendBuffer=reinterpret_cast<uintptr_t>(bytes);r.sendBytes=1;
    r.replyTransactionType=1;r.replyAddress=0xA8;
    r.replyBuffer=reinterpret_cast<uintptr_t>(&value);r.replyBytes=1;
}
template<class Request> void scdcWriteRequest(Request &r,uint8_t (&bytes)[2]) {
    r.sendTransactionType=1;r.sendAddress=0xA8;
    r.sendBuffer=reinterpret_cast<uintptr_t>(bytes);r.sendBytes=2;
}
struct SCDCResult {
    bool complete {},changed {},restored {};
    uint8_t before {},wanted {};
    uint32_t sourceFlags {};
};
template<class IO> SCDCResult negotiateSCDC(IO &io,uint64_t rateHz,
                                          uint64_t sinkMaxHz,bool scdc) {
    SCDCResult r;
    if(!rateHz || rateHz>sinkMaxHz || rateHz>600000000 || !io.connected())return r;
    const bool high=rateHz>340000000;
    if(!scdc) {r.complete=!high;return r;}
    uint8_t version=0;
    if(!io.read(1,version) || !version || !io.connected() ||
       !io.write(2,1) || !io.connected() || !io.read(0x20,r.before))return r;
    r.wanted=(r.before&~3U)|(high?3U:0U);
    const auto restore=[&]() {
        uint8_t actual=0;
        return io.connected() && io.write(0x20,r.before) && io.connected() &&
            io.read(0x20,actual) && actual==r.before && io.connected();
    };
    if(!io.connected())return r;
    if(r.wanted!=r.before) {
        r.changed=true;
        // A failed transfer may have reached the sink. Restore the previous
        // configuration on failure while this connection still owns DDC.
        if(!io.write(0x20,r.wanted)) {
            r.restored=restore();
            return r;
        }
    }
    uint8_t actual=0;
    if(!io.connected() || !io.read(0x20,actual) || actual!=r.wanted || !io.connected()) {
        if(r.changed)r.restored=restore();
        return r;
    }
    r.sourceFlags=high ? (1U<<4)|1U : 0; // high TMDS ratio + scrambling
    r.complete=true;
    return r;
}
}
#endif
