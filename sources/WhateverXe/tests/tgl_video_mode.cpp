#include "kern_tgl_video_mode.hpp"
#include <cassert>
#include <cstdio>
using namespace TGLVideoMode;
int main() {
 uint32_t shortMode[]={0x70800000,0x82};
 uint32_t longMode[]={0x70800004,0x82,0x002b030a,0x07000303,0,0};
 assert(matches(shortMode,2,Layout::ObservedShort));
 assert(!matches(shortMode,2,Layout::ReferenceAVCNV12SixDWORD));
 assert(matches(longMode,6,Layout::ReferenceAVCNV12SixDWORD));
 assert(!matches(longMode,6,Layout::ObservedShort));
 assert(!matches(longMode,6,static_cast<Layout>(99)));
 for(unsigned n=0;n<6;n++)assert(!matches(longMode,n,Layout::ReferenceAVCNV12SixDWORD));
 for(unsigned n=0;n<2;n++)assert(!matches(shortMode,n,Layout::ObservedShort));
 assert(!matches(nullptr,6,Layout::ReferenceAVCNV12SixDWORD));
 for(unsigned i=0;i<6;i++)for(unsigned b=0;b<32;b++) {
  longMode[i]^=1U<<b;assert(!matches(longMode,6,Layout::ReferenceAVCNV12SixDWORD));longMode[i]^=1U<<b;
 }
 puts("PASS mode policies: explicit layout, exact field matrix, all192 bit mutations, truncations and invalid policy");
}
