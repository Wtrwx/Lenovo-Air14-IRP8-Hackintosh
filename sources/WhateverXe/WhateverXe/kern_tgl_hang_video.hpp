// Failure-only AVC VDEnc resource prefixes. Intel Gen12 MFX/VDENC hwcmd layouts.
// Addresses are candidates until the caller qualifies ownership and each PTE.
#ifndef kern_tgl_hang_video_hpp
#define kern_tgl_hang_video_hpp
#include "kern_tgl_ppgtt.hpp"
#include "kern_tgl_hang_refs.hpp"
namespace TGLHangVideo {
using Capture=TGLHangRefs::Capture;
inline bool admitted(bool flag,uint32_t identity,unsigned revision,bool tahoe){return flag && identity==0xA7A08086 && revision==4 && tahoe;}
inline bool ownedEngine(unsigned regs,unsigned ring){return (regs==0 || regs==2 || regs==3) && ring==regs;}
inline unsigned length(uint32_t h){
 if(h==0)return 1;
 if(h==0x13000083 || h==0x13000003 || h==0x10000003)return 5;
 if(h==0x12000002)return 4;
 switch(h){
 case 0x70000003:case 0x70010004:case 0x70020042:case 0x70030018:case 0x70040008:
 case 0x70800000:case 0x70810004:case 0x70820004:case 0x70830004:case 0x70840023:
 case 0x7086003b:case 0x71000013:case 0x70850021:case 0x70070010:case 0x70080020:
 case 0x71030009:case 0x70870002:case 0x77800000:return (h&4095)+2;
 default:return (h&0xfffff000U)==0x70480000U ? (h&4095)+2 : 0;
 }
}
template<class ReadPage>
void collect(Capture &c,const uint8_t *b,unsigned n,uint64_t base,uint64_t bb,ReadPage read){
 c.q[0]=1;c.q[3]=base;c.q[4]=bb;c.q[8]=1;
 if(!b || n<20 || n>16384 || bb<base || bb-base>n-4 || ((bb-base)&3))return;
 const unsigned end=unsigned(bb-base);unsigned anchor=n,mp=n,vp=n,ind=n,mode=n,image=n,slice=n;
 // Only structural starts ending exactly at the active command are admitted.
 for(unsigned a=0;a<=end && a+20<=n;a+=4)if(TGLHangRefs::word(b+a)==0x70000003 && (TGLHangRefs::word(b+a+4)&31)==0x12){
  unsigned o=a;
  while(o<end){unsigned dw=length(TGLHangRefs::word(b+o));if(!dw || dw>(n-o)/4 || dw*4>end-o)break;o+=dw*4;}
  if(o==end){if(anchor!=n){c.q[8]=2;return;}anchor=a;}
 }
 if(anchor==n)return;c.q[5]=base+anchor;
 for(unsigned o=anchor;o<=end;){
  const uint32_t h=TGLHangRefs::word(b+o);unsigned dw=length(h);if(!dw || dw>(n-o)/4){c.q[8]=3;return;}
  unsigned *target=nullptr;
  if(h==0x70020042)target=&mp;if(h==0x70840023)target=&vp;if(h==0x70030018)target=&ind;
  if(h==0x70800000)target=&mode;if(h==0x71000013)target=&image;
  if(target){if(*target!=n){c.q[8]=4;return;}*target=o;}
  if(h==0x71030009){if(TGLHangRefs::word(b+o+4)!=2){c.q[8]=5;return;}slice=o;}
  if(o==end)break;o+=dw*4;
 }
 if(mp==n || vp==n || ind==n || mode==n || image==n || slice==n ||
    (TGLHangRefs::word(b+mode+4)&15)!=2){c.q[8]=6;return;}
 c.q[22]=TGLHangRefs::word(b+anchor+4);c.q[23]=TGLHangRefs::word(b+mode+4);
 auto address=[&](unsigned o,unsigned i){return TGLHangRefs::pair(b+o+i*4)&~63ULL;};
 // q10..21 roles: MFX pre/post/raw/streamout/intra/deblock, VDEnc raw/
 // streamin/rowstore/statistics, PAK-BSE lower/upper (prefixes, not full images).
 const unsigned offsets[]={mp,mp,mp,mp,mp,mp,vp,vp,vp,vp,ind,ind};
 const unsigned words[]={1,4,7,10,13,16,10,13,16,34,21,24};
 for(unsigned i=0;i<12;++i)c.q[10+i]=address(offsets[i],words[i]);
 // Cache-select rowstores are internal offsets, not GPU virtual addresses.
 uint64_t cached=0;
 const unsigned rowstores[]={4,5,8};
 for(unsigned i:rowstores)if(TGLHangRefs::word(b+offsets[i]+(words[i]+2)*4)&4096)cached|=1ULL<<i;
 c.q[24]=cached;c.q[8]=0;
 for(unsigned i=0;i<12;++i){uint64_t gpu=c.q[10+i];if(!gpu || gpu>>47 || (cached&(1ULL<<i)))continue;
  // Up to two pages per role; total capped by Capture::MaxPages, explicit drops.
  for(unsigned page=0;page<2;++page){uint64_t at=(gpu&~4095ULL)+page*4096;if(at>>47)continue;
   unsigned j=0;while(j<c.q[6] && c.pages[j].q[0]!=at)++j;
   if(j<c.q[6]){c.pages[j].q[1]|=1ULL<<i;continue;}
   if(c.q[6]>=TGLHangRefs::MaxPages){++c.q[7];continue;}
   auto &p=c.pages[c.q[6]++];p.q[0]=at;p.q[1]=1ULL<<i;read(p);
  }
 }
}
}
#endif
