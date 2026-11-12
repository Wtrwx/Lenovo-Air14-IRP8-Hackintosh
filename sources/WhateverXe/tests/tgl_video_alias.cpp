#include "kern_tgl_video_alias.hpp"
#include <cassert>
#include <map>
#include <cstdio>
using namespace TGLVideoAlias;
struct Fixture {
 static constexpr uint64_t K=0xffff800000000000ULL,R=K+0x10000,C=K+0x20000,O=K+0x30000,
  T=K+0x40000,P=K+0x50000,L=K+0x60000,B=K+0x70000,CPU0=K+0x4000000,
  CR=K+0x9000000,Class=K+0xa000000,G=0x40010000;
 std::map<uint64_t,uint8_t> memory;
 unsigned reads=0,failAt=0;bool physicalFailure=false,badAlias=false;
 void put(uint64_t p,uint64_t v,unsigned n=8){for(unsigned i=0;i<n;++i)memory[p+i]=uint8_t(v>>(i*8));}
 uint64_t read(uint64_t p,void *out,unsigned n){
  assert(n && n<=8 && kernelRange(p,n));++reads;if(reads==failAt)return 0;
  for(unsigned i=0;i<n;++i)if(!memory.count(p+i))return 0;
  for(unsigned i=0;i<n;++i)static_cast<uint8_t*>(out)[i]=memory[p+i];return n;
 }
 TGLVM::Mapping walk(uint64_t root,uint64_t address){
  TGLVM::Mapping m{};if(physicalFailure || root!=CR){m.status=1;return m;}
  m.shift=12;m.reads=4;m.entry[0]=0x1003;m.entry[1]=0x2003;m.entry[2]=0x3003;
  const auto page=address&~4095ULL;
  if(page==CPU0 || page==CPU0+4096)m.physical=0x500000+(address-CPU0)+(badAlias?4096:0);
  else if(page>=K+0x200000 && page<K+0x204000)m.physical=0x1000+(address-(K+0x200000));
  else {m.status=2;return m;}
  m.entry[3]=(m.physical&~4095ULL)|3;return m;
 }
 Fixture(){
  put(R+0x20,C);put(R+0x10,O);put(R+0x18,L);put(R+0x40,3,1);
  put(C+0x50,O);put(C+0xb0,R);put(C+0x58,T);put(C+0x98,B);put(C+0x7c,241,4);
  put(T+0x260,P);put(P,Class);put(B+0x38,L);put(B+0x20,8192);
  put(L+0x10c4,0,4);put(L+0x10cc,0x1000,4);put(K+0x50,CR);
  put(P+0x40,K+0x100000);put(P+0x48,K+0x300000);
  const unsigned shifts[]={39,30,21};
  for(unsigned i=0;i<4;++i){put(K+0x100000+i*4096+0x18,(i+1)*4096);put(K+0x100000+i*4096+0x20,K+0x200000+i*4096);}
  for(unsigned i=0;i<3;++i){
   auto entry=K+0x300000+i*0x4000+((G>>shifts[i])&511)*32;
   put(entry,K+0x100000+(i+1)*4096);
   if(i<2)put(entry+8,K+0x300000+(i+1)*0x4000);
   put(K+0x200000+i*4096+((G>>shifts[i])&511)*8,(i+2)*4096|3);
  }
  put(K+0x203000+((G>>12)&511)*8,0x500003);
  put(K+0x203000+(((G>>12)&511)+1)*8,0x501003);
 }
 bool beginReceipt(Receipt &r,uint64_t bytes=4096,uint64_t offset=4080){
  auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};
  auto cpu=[&](uint64_t root,uint64_t address){return walk(root,address);};
  return begin(r,R,3,Class,CPU0+offset,G+offset,bytes,K,rd,cpu);
 }
 bool endReceipt(Receipt &r){
  auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};
  auto cpu=[&](uint64_t root,uint64_t address){return walk(root,address);};
  return finish(r,K,rd,cpu);
 }
};
int main(int argc,char **argv){
 static_assert(sizeof(Page)==944 && sizeof(Receipt)==2176,"decoder layouts");
 Fixture f;Receipt r;const auto original=f.memory;
 assert(f.beginReceipt(r) && r.q[BeforeQualified] && r.q[Status]==NotChecked && r.q[Pages]==2);
 assert(r.pages[0].q[2]==16 && r.pages[1].q[2]==4080);
 assert(f.endReceipt(r) && r.q[Status]==Ready && r.q[Finished]==1 && f.memory==original);
 if(argc==2){auto file=fopen(argv[1],"wb");assert(file && fwrite(&r,sizeof(r),1,file)==1);assert(!fclose(file));}
 const unsigned completeReads=f.reads;
 for(unsigned fail=1;fail<=completeReads;++fail){Fixture x;x.failAt=fail;Receipt z;const auto before=x.memory;
  const bool started=x.beginReceipt(z),ended=x.endReceipt(z);assert(!(started&&ended));assert(x.memory==before);}
 {Fixture x;Receipt z;assert(x.beginReceipt(z,4096,0)&&z.q[Pages]==1&&x.endReceipt(z));}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,4097)&&z.q[Status]==Bounds);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,0)&&z.q[Status]==Bounds);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,4095)&&z.q[Status]==Bounds);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,16,4081)&&z.q[Status]==Bounds);}
 for(unsigned bad=0;bad<4;++bad){Fixture x;Receipt z;
  auto read=[&](uint64_t p,void *d,unsigned n){return x.read(p,d,n);};auto walk=[&](uint64_t root,uint64_t p){return x.walk(root,p);};
  assert(!begin(z,Fixture::R,bad==0?2:3,Fixture::Class,Fixture::CPU0, bad==1?Fixture::G+4:bad==2?(1ULL<<47):Fixture::G,bad==3?8192:4096,Fixture::K,read,walk));assert(z.q[Status]==Bounds);}
 {Fixture x;Receipt z;x.put(Fixture::R+0x40,0,1);auto rd=[&](uint64_t p,void *d,unsigned n){return x.read(p,d,n);};auto cpu=[&](uint64_t root,uint64_t p){return x.walk(root,p);};assert(begin(z,Fixture::R,0,Fixture::Class,Fixture::CPU0,Fixture::G,4096,Fixture::K,rd,cpu)&&x.endReceipt(z));}
 for(unsigned bad=0;bad<11;++bad){Fixture x;Receipt z;
  if(bad==0)x.put(Fixture::C+0xb0,0);
  if(bad==1)x.put(Fixture::C+0x50,0);
  if(bad==2)x.put(Fixture::P,Fixture::Class+8);
  if(bad==3)x.put(Fixture::B+0x20,4096);
  if(bad==4)x.put(Fixture::L+0x10cc,0x6000,4);
  if(bad==5)x.put(Fixture::K+0x50,0);
  if(bad==6)x.physicalFailure=true;
  if(bad==7)x.badAlias=true;
  if(bad==8)x.put(Fixture::K+0x203000+((Fixture::G>>12)&511)*8,0x500002);
  if(bad==9)x.put(Fixture::K+0x202000+((Fixture::G>>21)&511)*8,0x4083);
  if(bad==10)x.put(Fixture::K+0x203000+(((Fixture::G>>12)&511)+1)*8,0x600003);
  assert(!x.beginReceipt(z));assert(z.q[Status]!=Ready && !z.q[BeforeQualified]);
 }
 for(unsigned bad=0;bad<4;++bad){Fixture x;Receipt z;assert(x.beginReceipt(z));
  if(bad==0)x.put(Fixture::C+0x7c,242,4);
  if(bad==1)x.put(Fixture::K+0x50,Fixture::CR+4096);
  if(bad==2)x.put(Fixture::K+0x203000+((Fixture::G>>12)&511)*8,0x50000b);
  if(bad==3)x.badAlias=true;
  assert(!x.endReceipt(z));assert(z.q[Status]==(bad==0?OwnerChanged:bad==1?RootChanged:bad==2?MappingChanged:AliasMismatch));
 }
 assert(kernelRange(UINT64_MAX,1)&&!kernelRange(UINT64_MAX,2)&&!kernelRange(0,1));
 printf("PASS bounded submission aliases: one/two pages, exact physical alias, CPU/GPU chain, all %u read failures, ownership/root/PTE mutations, no writes\n",completeReads);
}
