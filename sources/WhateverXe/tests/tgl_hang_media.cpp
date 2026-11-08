#include "kern_tgl_hang_media.hpp"
#include <memory>
#include <vector>
#include <map>
#include <cassert>
#include <cstdio>
using namespace TGLHangMedia;
static void put(std::vector<uint8_t> &b,unsigned off,uint32_t v){memcpy(b.data()+off,&v,4);}
int main(){
 // Command bytes from the measured RCS packet shape; GPU addresses are fixtures.
 const uint32_t raw[]={0x11000001U,0x0U,0x80000040U,0x7a000004U,0x1010a0U,0x0U,0x0U,0x0U,0x0U,0x69041301U,0x6101000eU,0x1U,0x0U,0x0U,0x409a1001U,0x0U,0x4099f001U,0x0U,0x1U,0x0U,0x409a0001U,0x0U,0x1U,0x1001U,0x1U,0x1001U,0x70000007U,0x0U,0x0U,0x22f1000U,0x0U,0x7c0002U,0x0U,0x0U,0x0U,0x70010002U,0x0U,0x40U,0x400U,0x70020002U,0x0U,0x40U,0x0U,0x7103000fU,0x0U,0x0U,0x0U,0x0U,0x0U,0x0U,0x7ff07ffU,0x20044U,0x0U,0x0U,0x10000U,0x1U,0x20044U,0x0U,0x44U,0x20000U,0x7a000004U,0x1010a0U,0x0U,0x0U,0x0U,0x0U,0x7a000004U,0x89cU,0x0U,0x0U,0x0U,0x0U};
 std::vector<uint8_t> batch(sizeof(raw));memcpy(batch.data(),raw,sizeof(raw));
 const auto saved=batch;constexpr uint64_t Base=0x4098f000,D=0x4099f000,I=0x409a0000,S=0x409a1000;
 std::map<uint64_t,std::vector<uint8_t>> pages;
 for(uint64_t a:{D,I,I+4096,S,S+0x12000,0x50000000ULL})pages[a].resize(4096);
 // Kernel offset zero is valid. Binding pointer exercises Gen12 bits above 15.
 put(pages[D],16,0x12000);put(pages[S+0x12000],0,0x80);
 put(pages[S],0x80,1U<<29);put(pages[S],0xa0,0x50000000);
 unsigned reads=0;bool hole=false,partial=false;
 auto read=[&](TGLHangRefs::Page &p){
  ++reads;assert(!(p.q[0]&4095) && p.q[0]<(1ULL<<47));p.q[4]=1;
  auto f=pages.find(p.q[0]);if(hole || f==pages.end())return;
  p.q[2]=partial?4095:4096;p.q[3]=!partial;p.q[4]=partial?2:0;memcpy(p.bytes,f->second.data(),4096);
 };
 auto c=std::make_unique<Capture>();
 TGLHangMedia::collect(*c,batch.data(),batch.size(),Base,Base+0x108,read);
 assert(!c->q[8] && c->q[12]==D && c->q[13]==I && c->q[14]==S+0x12000 && c->q[15]==D+0x400);
 bool pixel=false,shader=false;for(unsigned j=0;j<c->q[6];++j){pixel|=c->pages[j].q[0]==0x50000000;shader|=(c->pages[j].q[1]&TGLHangRefs::Shader)!=0;}
 assert(pixel&&shader&&batch==saved);
 for(unsigned n=0;n<0x10c;++n){*c={};reads=0;TGLHangMedia::collect(*c,batch.data(),n,Base,Base+0x108,read);assert(c->q[8]&&reads==0);}
 for(unsigned bit:{0U,1U}){*c={};hole=!bit;partial=bit;TGLHangMedia::collect(*c,batch.data(),batch.size(),Base,Base+0x108,read);assert(c->q[8]==7 && c->q[6]<=24);}hole=partial=false;
 auto bad=batch;put(bad,0xa8,0x20);*c={};reads=0;TGLHangMedia::collect(*c,bad.data(),bad.size(),Base,Base+0x108,read);assert(c->q[8]==6 && !reads);
 // Ambiguous state mutation / branch is not interpreted as a media submission.
 bad=batch;put(bad,0xf0,0x18800001);*c={};reads=0;TGLHangMedia::collect(*c,bad.data(),bad.size(),Base,Base+0x108,read);assert(c->q[8] && !reads);
 uint32_t seed=0x32499;for(unsigned k=0;k<20000;++k){
  bad=batch;seed=seed*1664525+1013904223;unsigned at=seed%bad.size();bad[at]^=uint8_t(seed>>24);
  *c={};reads=0;TGLHangMedia::collect(*c,bad.data(),bad.size(),Base,Base+0x108,read);assert(c->q[6]<=24 && reads<=24);
 }
 puts("PASS media references: descriptor/SBA addressing, real packet shape, explicit holes, truncation, branches, 20000 bounded mutations");
}
