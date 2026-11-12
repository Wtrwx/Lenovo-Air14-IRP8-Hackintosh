#include "kern_tgl_video_map.hpp"
#include <map>
#include <cassert>
#include <cstdio>
using namespace TGLVideoMap;
struct Fixture {
 static constexpr uint64_t K=0xffff800000000000ULL,M=K+0x100000,T=K+0x200000,
  A=K+0x300000,B=K+0x301000,X=K+0x400000,Y=K+0x401000,G=0x100000;
 std::map<uint64_t,uint8_t> memory;unsigned reads=0,failAt=0;bool shortRead=false,changeList=false;
 void put(uint64_t p,uint64_t v,unsigned n=8){for(unsigned i=0;i<n;++i)memory[p+i]=uint8_t(v>>(i*8));}
 void bytes(uint64_t p,const uint8_t *b,unsigned n){for(unsigned i=0;i<n;++i)memory[p+i]=b[i];}
 void zero(uint64_t p,unsigned n){for(unsigned i=0;i<n;++i)memory[p+i]=0;}
 void loadNative(const char *name) {
  // Native data must replace the model module completely: no synthetic getter
  // or virtual slot may silently satisfy a missing native fixture segment.
  for(auto it=memory.lower_bound(M);it!=memory.end()&&it->first<M+0x806a0;)it=memory.erase(it);
  auto fp=fopen(name,"rb");assert(fp);uint32_t count=0;assert(fread(&count,4,1,fp)==1&&count==9);
  for(unsigned i=0;i<count;++i){uint64_t offset=0;uint32_t n=0;uint8_t data[4096];
   assert(fread(&offset,8,1,fp)==1&&fread(&n,4,1,fp)==1&&n<=sizeof(data)&&offset+n<=0x806a0);
   assert(fread(data,1,n,fp)==n);bytes(M+offset,data,n);}
  assert(fgetc(fp)==EOF&&!fclose(fp));
 }
 uint64_t read(uint64_t p,void *out,unsigned n) {
  ++reads;assert(kernelRange(p,n)&&n<=32);
  if(reads==failAt && !shortRead)return 0;
  const unsigned count=reads==failAt?n-1:n;
  for(unsigned i=0;i<count;++i){auto it=memory.find(p+i);if(it==memory.end())return i;static_cast<uint8_t*>(out)[i]=it->second;}
  // Change a list only after its initial header was captured.
  if(changeList && p==B+0x98)put(T+0x200+16,2,4);
  return count;
 }
 Fixture() {
  uint32_t h[]={0xfeedfacf,0x1000007,3,11,9,1488,0x80000085,0};
  bytes(M,reinterpret_cast<uint8_t*>(h),sizeof(h));put(M+32,0x1b,4);put(M+36,24,4);
  const uint8_t uuid[]={0x6c,0x4a,0x16,0xb1,0x66,0xdc,0x3a,0x8b,0x9f,0x0b,0xd3,0xe0,0x27,0xb1,0x6e,0xb1};bytes(M+40,uuid,16);
  for(unsigned i=0;i<8;++i){put(M+56+8*i,0,4);put(M+60+8*i,i==7?1408:8,4);}
  const uint64_t slots[]={0x7ffe8+0x128,0x7ffe8+0x168,0x7ffe8+0x190,0x80570+0x120};
  const uint64_t targets[]={0x526ec,0x52bd4,0x52b50,0x5595a};for(unsigned i=0;i<4;++i)put(M+slots[i],M+targets[i]);
  const uint8_t gpu[]={0xf6,0x47,0x10,0x40,0x75,0x08,0x48,0x8b,0x87,0x98,0,0,0,0xc3};bytes(M+targets[0],gpu,sizeof(gpu));
  const uint8_t length[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x7f,0x18,0x48,0x8b,0x07,0x5d,0xff,0xa0,0x20,1,0,0};bytes(M+targets[1],length,sizeof(length));
  const uint8_t assigned[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x87,0xc8,0,0,0,0x5d,0xc3};bytes(M+targets[2],assigned,sizeof(assigned));
  const uint8_t mem[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x40,0x5d,0xc3};bytes(M+targets[3],mem,sizeof(mem));
  for(auto p:{A,B}){zero(p,0xd0);put(p,M+0x7ffe8);put(p+0xc,1,4);put(p+0x18,p==A?X:Y);
   put(p+0x90,T);put(p+0x88,K+0x500000);put(p+0x98,p==A?G:G+0x10000);put(p+0xc8,8192);}
  for(auto p:{X,Y}){zero(p,0x48);put(p,M+0x80570);put(p+0x40,4096);}
  zero(T+0x1e8,0x30);put(T+0x200,A);put(T+0x208,A);put(T+0x210,1,4);
  put(T+0x1e8,B);put(T+0x1f0,B);put(T+0x1f8,1,4);
 }
 bool beginReceipt(Receipt &r,uint64_t gpu=G,uint64_t n=4096){auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};return begin(r,T,gpu,n,rd);}
 bool endReceipt(Receipt &r){auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};return finish(r,rd);}
};
int main(int argc,char **argv) {
 Fixture f;if(argc==3)f.loadNative(argv[2]);Receipt r;const auto original=f.memory;
 assert(f.beginReceipt(r)&&f.endReceipt(r)&&r.q[Status]==DeclaredSpan&&f.memory==original);
 assert(r.samples[0].node[Address]==Fixture::A&&r.samples[0].q[2]==2&&r.samples[0].q[3]==1);
 assert(r.samples[0].node[AssignedLength]==8192&&r.samples[0].node[MemoryLength]==4096);
 if(argc>=2){auto fp=fopen(argv[1],"wb");assert(fp&&fwrite(&r,sizeof(r),1,fp)==1&&!fclose(fp));}
 unsigned complete=f.reads;
 for(bool partial:{false,true})for(unsigned fail=1;fail<=complete;++fail){Fixture x;x.failAt=fail;x.shortRead=partial;Receipt z;auto before=x.memory;
  const bool a=x.beginReceipt(z),b=x.endReceipt(z);assert(!(a&&b)&&x.memory==before&&z.q[Status]!=DeclaredSpan);}
 for(unsigned bad=0;bad<20;++bad){Fixture x;Receipt z;
  if(bad==0)x.put(Fixture::M+40,0,1);
  if(bad==1)x.put(Fixture::M+0x7ffe8+0x168,Fixture::M+0x52b50);
  if(bad==2)x.put(Fixture::M+0x5595a,0,1);
  if(bad==3)x.put(Fixture::T+0x210,129,4);
  if(bad==4)x.put(Fixture::A+0x28,Fixture::B);
  if(bad==5)x.put(Fixture::A+0x90,Fixture::T+8);
  if(bad==6)x.put(Fixture::X,Fixture::M+0x80578);
  if(bad==7)x.put(Fixture::A+0x10,0x40,4);
  if(bad==8)x.put(Fixture::A+0xc,0,4);
  if(bad==9)x.put(Fixture::A+0xb0,1);
  if(bad==10)x.put(Fixture::X+0x40,2048);
  if(bad==11)x.put(Fixture::A+0xc8,2048);
  if(bad==12)x.put(Fixture::B+0x98,Fixture::G);
  if(bad==13)x.put(Fixture::A+0x30,Fixture::A);
  if(bad==14)x.put(Fixture::T+0x1e8,Fixture::A);
  if(bad==15)x.put(Fixture::T+0x208,Fixture::B);
  if(bad==16)x.put(Fixture::M+36,0,4);
  if(bad==17)x.put(Fixture::M+16,10,4);
  if(bad==18)x.put(Fixture::B,Fixture::M+0x7fff0);
  if(bad==19)x.put(Fixture::A+0x98,(1ULL<<47)-2048);
  const auto before=x.memory;assert(!x.beginReceipt(z)&&!x.endReceipt(z)&&x.memory==before&&z.q[Status]!=DeclaredSpan);
 }
 for(unsigned bad=0;bad<5;++bad){Fixture x;Receipt z;assert(x.beginReceipt(z));
  if(bad==0)x.put(Fixture::A+0x20,1,4);
  if(bad==1)x.put(Fixture::B+0x20,1,4); // Even a nonselected map changes the list digest.
  if(bad==2)x.put(Fixture::A+0x18,Fixture::Y);
  if(bad==3)x.put(Fixture::T+0x210,0,4);
  if(bad==4)x.put(Fixture::X+0x40,8192);
  assert(!x.endReceipt(z)&&z.q[Status]!=DeclaredSpan);
 }
 {Fixture x;Receipt z;x.changeList=true;assert(!x.beginReceipt(z)&&z.q[BeforeStatus]==Changed);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,Fixture::G+4096,4)&&z.q[BeforeStatus]==NotContained);}
 {Fixture x;Receipt z;assert(x.beginReceipt(z,Fixture::G+4092,4)&&x.endReceipt(z));}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,Fixture::G+4092,8)&&z.q[BeforeStatus]==RangeOverrun);}
 {Fixture x;Receipt z;assert(x.beginReceipt(z,Fixture::G+0x10000,4096)&&x.endReceipt(z)&&z.samples[0].node[Address]==Fixture::B);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,0,4)&&z.q[BeforeStatus]==Bounds);}
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,Fixture::G,0)&&z.q[BeforeStatus]==Bounds);}
 // A later rejected node must not be reported with the earlier hit's fields.
 // Neither rejection relaxes containment or authorizes a target memory read.
 for(bool indirect:{false,true}){Fixture x;Receipt z;
  if(indirect)x.put(Fixture::B+0x10,0x40,4);else x.put(Fixture::B+0xc8,0);
  const auto before=x.memory;assert(!x.beginReceipt(z));
  assert(z.q[BeforeStatus]==(indirect?IndirectAddress:MapBounds));
  assert(z.samples[0].node[Address]==Fixture::B&&z.samples[0].q[6]==Fixture::B);
  assert(z.samples[0].q[3]==1&&x.memory==before);
 }
 {Fixture x;Receipt z;assert(!x.beginReceipt(z,(1ULL<<47)-4,8)&&z.q[BeforeStatus]==Bounds);}
 printf("PASS native map metadata: %u read fault positions x2, donor/list/ownership/span controls, sample changes, no writes or virtual calls\n",complete);
 return 0;
}
