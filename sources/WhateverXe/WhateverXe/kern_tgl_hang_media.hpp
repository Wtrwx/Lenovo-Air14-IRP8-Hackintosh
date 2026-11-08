// Read-only, failure-only media references. Never submits or edits GPU work.
// Intel TGL PRM Vol2a MEDIA_* / STATE_BASE_ADDRESS; Vol2d interface descriptor.
#ifndef kern_tgl_hang_media_hpp
#define kern_tgl_hang_media_hpp
#include "kern_tgl_ppgtt.hpp"
#include "kern_tgl_hang_refs.hpp"
namespace TGLHangMedia {
using Capture=TGLHangRefs::Capture;
template<class ReadPage>
void collect(Capture &c,const uint8_t *batch,unsigned n,uint64_t base,uint64_t bb,ReadPage read) {
 c.q[0]=1;c.q[3]=base;c.q[4]=bb;c.q[8]=1;
 if(!batch || n<128 || n>16384 || bb<base || bb-base>n-4 || ((bb-base)&3))return;
 const unsigned end=static_cast<unsigned>(bb-base);unsigned anchor=n;
 for(unsigned o=4;o+64<=n && o<=end;o+=4)
  if(TGLHangRefs::word(batch+o-4)==0x69041301 &&
     (TGLHangRefs::word(batch+o)==0x6101000e || TGLHangRefs::word(batch+o)==0x61010014) &&
     TGLHangRefs::walkable(batch,n,o,end))anchor=o;
 if(anchor==n)return;
 c.q[5]=base+anchor;const auto s=batch+anchor;
 if(!(TGLHangRefs::word(s+16)&1) || !(TGLHangRefs::word(s+24)&1) || !(TGLHangRefs::word(s+40)&1)){c.q[8]=2;return;}
 c.q[9]=TGLHangRefs::pair(s+16)&~4095ULL;
 c.q[10]=TGLHangRefs::pair(s+24)&~4095ULL;
 c.q[11]=TGLHangRefs::pair(s+40)&~4095ULL;
 unsigned ids=n,curbe=n,walker=n,vfe=n;
 for(unsigned o=anchor;o<end;) {
  const auto w=TGLHangRefs::word(batch+o);const unsigned dw=TGLHangRefs::length(w);
  if(!dw || dw>(n-o)/4){c.q[8]=3;return;}
  if(o!=anchor && w!=0 && w!=0x7a000004 && w!=0x70000007 && w!=0x70010002 && w!=0x70020002 && w!=0x7103000f){c.q[8]=3;return;}
  if(w==0x70000007){if(vfe!=n){c.q[8]=4;return;}vfe=o;}
  if(w==0x70010002){if(curbe!=n){c.q[8]=4;return;}curbe=o;}
  if(w==0x70020002){if(ids!=n){c.q[8]=4;return;}ids=o;}
  if(w==0x7103000f){if(walker!=n){c.q[8]=4;return;}walker=o;}
  o+=dw*4;
 }
 if(vfe==n || curbe==n || ids==n || walker==n || vfe>curbe || curbe>ids || ids>walker){c.q[8]=5;return;}
 const unsigned idBytes=TGLHangRefs::word(batch+ids+8),idOffset=TGLHangRefs::word(batch+ids+12);
 const unsigned which=TGLHangRefs::word(batch+walker+4)&63;
 const unsigned cbBytes=TGLHangRefs::word(batch+curbe+8),cbOffset=TGLHangRefs::word(batch+curbe+12);
 // Admit the measured two-descriptor, 64-byte CURBE path only.
 if(idBytes!=64 || cbBytes!=64 || (idOffset&63) || (cbOffset&63) || which>=2){c.q[8]=6;return;}
 auto addr=[](uint64_t b,uint64_t off)->uint64_t{return b && off<(1ULL<<47) && b<=(1ULL<<47)-1-off?b+off:0;};
 auto add=[&](uint64_t gpu,uint64_t kind)->TGLHangRefs::Page* {
  if(!gpu || gpu>>47)return nullptr;gpu&=~4095ULL;
  for(unsigned i=0;i<c.q[6];++i)if(c.pages[i].q[0]==gpu){c.pages[i].q[1]|=kind;return c.pages+i;}
  if(c.q[6]>=TGLHangRefs::MaxPages){++c.q[7];return nullptr;}
  auto &p=c.pages[c.q[6]++];p.q[0]=gpu;p.q[1]=kind;read(p);return &p;
 };
 auto span=[&](uint64_t gpu,unsigned bytes,uint64_t kind) {
  if(!gpu || gpu>>47 || !bytes || bytes>8192 || bytes>(1ULL<<47)-gpu)return;
  const uint64_t last=(gpu+bytes-1)&~4095ULL;
  for(uint64_t at=gpu&~4095ULL;at<=last;at+=4096)add(at,kind);
 };
 auto readable=[](const TGLHangRefs::Page *p){return p && p->q[2]==4096 && p->q[3]==1 && !p->q[4];};
 c.q[12]=addr(c.q[10],uint64_t(idOffset)+which*32);c.q[15]=addr(c.q[10],cbOffset);
 c.q[16]=TGLHangRefs::word(batch+vfe+12);c.q[17]=TGLHangRefs::word(batch+vfe+20);
 span(c.q[15],64,TGLHangRefs::Constant);
 auto d=add(c.q[12],TGLHangRefs::Table);
 if(!readable(d) || (c.q[12]&4095)>4064){c.q[8]=7;return;}
 const auto id=d->bytes+(c.q[12]&4095);
 const uint64_t kernel=(uint64_t(TGLHangRefs::word(id+4)&65535)<<32)|(TGLHangRefs::word(id)&~63U);
 c.q[13]=addr(c.q[11],kernel);span(c.q[13],8192,TGLHangRefs::Shader);
 const uint32_t sampler=TGLHangRefs::word(id+12),binding=TGLHangRefs::word(id+16);
 c.q[14]=addr(c.q[9],binding&0x1fffe0U);
 if((sampler>>2)&7)span(addr(c.q[10],sampler&~31U),256,TGLHangRefs::Sampler);
 span(c.q[14],128,TGLHangRefs::Table);
 for(unsigned i=0;i<32 && c.q[14];++i) {
  const uint64_t at=addr(c.q[14],i*4);auto t=add(at,TGLHangRefs::Table);
  if(!readable(t))continue;
  const uint32_t off=TGLHangRefs::word(t->bytes+(at&4095));if(!off || (off&63))continue;
  const uint64_t surface=addr(c.q[9],off);auto p=add(surface,TGLHangRefs::Surface);
  if(!readable(p) || (surface&4095)>4032)continue;
  const auto state=p->bytes+(surface&4095);
  // A candidate first data page only; planar/auxiliary interpretation omitted.
  if((TGLHangRefs::word(state)>>29)==1)add(TGLHangRefs::pair(state+32),TGLHangRefs::Pixels);
 }
 c.q[8]=0;
}
}
#endif
