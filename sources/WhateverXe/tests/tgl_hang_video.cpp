#include "kern_tgl_hang_video.hpp"
#include <vector>
#include <memory>
#include <cassert>
#include <cstdio>
using namespace TGLHangVideo;
int main(){
 for(bool flag:{false,true})for(uint32_t id:{0xA7A08086U,0xA7A18086U,0x9A498086U})for(unsigned rev:{0U,4U,5U})for(bool os:{false,true})
  assert(admitted(flag,id,rev,os)==(flag&&id==0xA7A08086U&&rev==4&&os));
 for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b)assert(ownedEngine(a,b)==(a==b&&(a==0||a==2||a==3)));
 std::vector<uint32_t> w;
 auto packet=[&](uint32_t h){unsigned o=w.size();w.resize(o+length(h));w[o]=h;return o;};
 auto pipe=packet(0x70000003);w[pipe+1]=0x12;
 auto mp=packet(0x70020042),ind=packet(0x70030018),mode=packet(0x70800000);w[mode+1]=0x82;
 auto vp=packet(0x70840023);packet(0x71000013);auto slice=packet(0x71030009);w[slice+1]=2;
 packet(0x70480003);packet(0x70870002);packet(0x77800000);packet(0x13000083);
 auto active=packet(0x71030009);w[active+1]=2;
 unsigned loc[]={mp+1,mp+4,mp+7,mp+10,mp+13,mp+16,vp+10,vp+13,vp+16,vp+34,ind+21,ind+24};
 for(unsigned i=0;i<12;++i)w[loc[i]]=0x50000000+i*0x4000;
 auto c=std::make_unique<Capture>();unsigned reads=0;bool hole=false;
 auto read=[&](TGLHangRefs::Page &p){assert(!(p.q[0]&4095)&&p.q[0]<(1ULL<<47));++reads;p.q[2]=hole?0:4096;p.q[3]=!hole;p.q[4]=hole?1:0;};
 auto run=[&](const std::vector<uint32_t> &v,unsigned n){*c={};reads=0;TGLHangVideo::collect(*c,reinterpret_cast<const uint8_t *>(v.data()),n,0x40010000,0x40010000+active*4,read);};
 auto page=w;page.resize(1024);run(page,4096);assert(!c->q[8]&&reads==24);
 const auto saved=w;run(w,w.size()*4);assert(!c->q[8]&&reads==24&&c->q[6]==24&&w==saved);for(unsigned i=0;i<12;++i)assert(c->q[10+i]==w[loc[i]]);
 // Six-DWORD mode shifts following resources, but does not change their roles.
 // Header-looking extension words are payload and must not become duplicates.
 {auto extended=w;extended[mode]=0x70800004;
  extended.insert(extended.begin()+mode+2,{0x70800000,0x70800004,0,0});auto before=extended;
  auto collectExtended=[&](const std::vector<uint32_t>&v,unsigned offset){*c={};reads=0;TGLHangVideo::collect(*c,reinterpret_cast<const uint8_t*>(v.data()),v.size()*4,0x40010000,0x40010000+offset*4,read);};
  assert(length(0x70800004)==6);collectExtended(extended,active+4);
  assert(!c->q[8]&&reads==24&&c->q[6]==24&&c->q[23]==0x82&&extended==before);
  for(unsigned i=0;i<12;++i)assert(c->q[10+i]==w[loc[i]]);
  auto duplicate=extended;duplicate.insert(duplicate.begin()+mode+6,{0x70800000,0x82});
  collectExtended(duplicate,active+6);assert(c->q[8]==4&&reads==0);
  auto headerOnly=w;headerOnly[mode]=0x70800004;
  collectExtended(headerOnly,active);assert(c->q[8]&&reads==0);
 }
 auto duplicate=w;duplicate[vp+10]=duplicate[mp+7];run(duplicate,duplicate.size()*4);assert(!c->q[8]&&reads==22);bool merged=false;for(unsigned i=0;i<c->q[6];++i)merged|=(c->pages[i].q[1]&((1<<2)|(1<<6)))==((1<<2)|(1<<6));assert(merged);
 auto cached=w;for(unsigned i:{4U,5U,8U})cached[loc[i]+2]|=4096;run(cached,cached.size()*4);assert(!c->q[8]&&reads==18&&c->q[24]==((1<<4)|(1<<5)|(1<<8)));
 hole=true;run(w,w.size()*4);assert(!c->q[8]&&reads==24&&c->pages[0].q[4]==1);hole=false;
 for(unsigned n=0;n<(active+11)*4;++n){run(w,n);assert(c->q[8]&&reads==0);}
 auto branch=w;branch[mp]=0x18800001;run(branch,branch.size()*4);assert(c->q[8]&&reads==0);
 auto inter=w;inter[slice+1]=0;run(inter,inter.size()*4);assert(c->q[8]==5&&reads==0);
 auto high=w;high[mp+2]=0xffff;run(high,high.size()*4);assert(!c->q[8]&&reads==22);
 uint32_t seed=0x773128;for(unsigned k=0;k<30000;++k){auto v=w;seed=seed*1664525+1013904223;unsigned at=seed%v.size();v[at]^=seed;run(v,v.size()*4);assert(reads<=24&&c->q[6]<=24);}
 puts("PASS video resources: ownership engine pairs, bounded prefixes, cache offsets, holes, truncation, branch/inter rejection, 30000 mutations");
}
