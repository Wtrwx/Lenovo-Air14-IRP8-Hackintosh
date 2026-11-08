#include "kern_display13_hdmi_caps.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace Display13HDMI;
static void seal(uint8_t (&b)[128]) {unsigned s=0;b[127]=0;for(auto v:b)s+=v;b[127]=uint8_t(-s);}
int main() {
    uint8_t b[128] {2,3,4,0};seal(b);
    auto c=extension(b,128);assert(c.valid && !c.hdmi && sourceLimitHz(c,300000000)==300000000);
    // Generic HDMI 2.0 sink, independent of resolution, vendor and product.
    const uint8_t vsdb[]={0x67,0xd8,0x5d,0xc4,1,120,0x80,0};
    memcpy(b+4,vsdb,sizeof(vsdb));b[2]=12;seal(b);
    c=extension(b,128);assert(c.valid && c.hdmi && c.scdc && c.maxKHz==600000);
    assert(sourceLimitHz(c,300000000)==340000000 && sourceLimitHz(c,165000000)==165000000);
    assert(sourceLimitHz(c,300000000,true)==600000000);
    assert(sourceLimitHz(c,720000000)==720000000);
    b[10]=0;seal(b);c=extension(b,128);assert(!c.scdc && c.maxKHz==340000);
    assert(sourceLimitHz(c,300000000,true)==340000000);
    b[9]=60;seal(b);c=extension(b,128);assert(c.maxKHz==300000 && sourceLimitHz(c,300000000)==300000000);
    b[9]=200;b[10]=0x80;seal(b);assert(extension(b,128).maxKHz==600000);
    b[127]^=1;assert(!extension(b,128).valid);b[127]^=1;
    assert(!extension(b,127).valid && !extension(nullptr,128).valid);
    b[2]=11;seal(b);assert(!extension(b,128).valid);
    b[2]=0;seal(b);assert(!extension(b,128).valid);
    uint8_t legacy[128] {2,3,12,0,0x67,3,12,0,0,0,0,68};seal(legacy);
    c=extension(legacy,128);assert(c.valid && c.hdmi && c.maxKHz==340000);
    puts("PASS generic HDMI sink clock limits: SCDC, legacy HDMI, DVI, malformed EDID and source-cap guards. CPU only.");
}
