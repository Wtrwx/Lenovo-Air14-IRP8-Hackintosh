#include "kern_display13_hdmi_caps.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace Display13HDMI;
static void seal(uint8_t (&b)[128]) {b[127]=0;unsigned sum=0;for(auto v:b)sum+=v;b[127]=uint8_t(-sum);}
int main(){
 uint8_t base[128] {0,255,255,255,255,255,255,0};base[126]=3;seal(base);
 uint8_t legacy[128] {2,3,12,0,0x67,3,12,0,0,0,0,68};seal(legacy);
 uint8_t forum[128] {2,3,12,0,0x67,0xd8,0x5d,0xc4,1,120,0x80,0};seal(forum);
 uint8_t other[128] {0x70};seal(other);
 EDIDCache e;
 assert(!e.observe(2,forum,true) && !e.caps().valid);
 assert(e.observe(1,base,true) && e.missing(2) && e.missing(4) && !e.missing(5));
 assert(e.observe(2,legacy,true) && e.caps().maxKHz==340000);
 assert(e.observe(3,other,true) && e.caps().maxKHz==340000);
 assert(e.observe(4,forum,true) && e.caps().scdc && e.caps().maxKHz==600000);
 assert(!e.missing(4) && sourceLimitHz(e.caps(),300000000,true)==600000000);
 // Extension rereads replace old Forum state, including loss of scrambling.
 forum[10]=0;seal(forum);e.observe(4,forum,true);
 assert(!e.caps().scdc && e.caps().maxKHz==340000);
 forum[9]=60;forum[10]=0x80;seal(forum);e.observe(4,forum,true);
 assert(e.caps().maxKHz==300000); // Forum maximum overrides legacy 340.
 // Order does not matter: a later legacy block cannot override Forum limits.
 e.observe(2,other,true);e.observe(3,legacy,true);assert(e.caps().maxKHz==300000);
 forum[127]^=1;e.observe(4,forum,true);assert(!e.caps().scdc && e.caps().maxKHz==340000);
 e.observe(3,legacy,false);assert(!e.caps().hdmi);
 // A new base clears all extension capabilities, even with the same port.
 base[126]=0;seal(base);e.observe(1,base,true);
 assert(!e.caps().valid && !e.missing(2) && !e.observe(2,legacy,true));
 base[126]=255;seal(base);e.observe(1,base,true);
 assert(e.missing(8) && !e.missing(9) && !e.observe(9,legacy,true));
 e.observe(1,base,false);assert(!e.baseValid && !e.missing(2));
 e.observe(1,nullptr,true);assert(!e.baseValid);
 base[0]=1;seal(base);e.observe(1,base,true);assert(!e.baseValid);
 base[0]=0;seal(base);base[127]^=1;e.observe(1,base,true);assert(!e.baseValid);
 puts("PASS HDMI multi-extension capabilities: later Forum VSDB, replacement, order, checksum, hotplug reset and exact donor block bound. CPU only.");
}
