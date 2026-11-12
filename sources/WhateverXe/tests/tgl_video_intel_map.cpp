#define main base_map_fixture_main
#include "tgl_video_map.cpp"
#undef main
struct IntelFixture:Fixture {
 static constexpr uint64_t I=K+0x800000,S=K+0xa00000;
 IntelFixture(){
  zero(I,0x1000);put(I,0xfeedfacf,4);put(I+4,0x1000007,4);put(I+12,11,4);put(I+16,1,4);put(I+20,24,4);put(I+32,0x1b,4);put(I+36,24,4);
  const uint8_t uuid[]={0xba,0x3a,0xa1,0xc0,0xfe,0x6b,0x33,0xb3,0x9d,0x85,0x73,0xf8,0x48,0x39,0x4e,0x3d};bytes(I+40,uuid,16);
  put(I+0xc8130,M+0x7ffd8);put(I+0xc8230,S);put(S,12,1);
  put(I+0xcd030,M+0x526ec);put(I+0xcd070,I+0x1118a);put(I+0xcd098,M+0x52b50);put(I+0xcd808,I+0x12afe);
  const uint8_t code[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8b,0x05,0x96,0x6f,0x0b,0,0xff,0x90,0x78,1,0,0,0x8b,0x8b,0x18,1,0,0,0xc1,0xe9,0x11,0x0f,0xb6,0xd1,0x48,0x8b,0x0d,0x7d,0x70,0x0b,0,0x8a,0x09,0xd3,0xe2,0x48,0x63,0xca,0x48,0x01,0xc8,0x48,0x83,0xc4,8,0x5b,0x5d,0xc3};bytes(I+0x1118a,code,sizeof(code));
  const uint8_t mem[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x40,0x5d,0xc3};bytes(I+0x12afe,mem,10);
  zero(A,0x120);put(A,I+0xccf08);put(A+0xc,1,4);put(A+0x18,X);put(A+0x90,T);put(A+0x88,K+0x500000);put(A+0x98,G);put(A+0xc8,8192);put(A+0x118,1U<<17,4);put(X,I+0xcd6e8);
 }
};
#ifndef TGL_INTEL_MAP_FIXTURE_ONLY
int main(int argc,char **argv){
 IntelFixture f;Receipt r;auto before=f.memory;
 assert(f.beginReceipt(r,Fixture::G+4096,4096)&&f.endReceipt(r)&&f.memory==before);
 auto &n=r.samples[0].node;assert(n[MemoryLength]==4096&&n[NativeMapLength]==8192&&n[ExtraPages]==1&&n[PageShift]==12&&n[IntelModule]==IntelFixture::I);
 assert(r.samples[0].q[4]==Fixture::M);if(argc==2){auto fp=fopen(argv[1],"wb");assert(fp&&fwrite(&r,sizeof(r),1,fp)==1&&!fclose(fp));}unsigned total=f.reads;
 for(bool partial:{false,true})for(unsigned fail=1;fail<=total;++fail){IntelFixture x;x.failAt=fail;x.shortRead=partial;Receipt z;auto saved=x.memory;bool a=x.beginReceipt(z),b=x.endReceipt(z);assert(!(a&&b)&&x.memory==saved&&z.q[Status]!=DeclaredSpan);}
 for(unsigned bad=0;bad<12;++bad){IntelFixture x;Receipt z;
  if(bad==0)x.put(IntelFixture::I+40,0,1);
  if(bad==1)x.put(IntelFixture::I+0xcd030,IntelFixture::I+0x1118a);
  if(bad==2)x.put(IntelFixture::I+0xcd070,Fixture::M+0x52bd4);
  if(bad==3)x.put(IntelFixture::I+0xcd098,Fixture::M+0x52bd4);
  if(bad==4)x.put(IntelFixture::I+0xcd808,Fixture::M+0x5595a);
  if(bad==5)x.put(IntelFixture::I+0x1118a+33,0,1);
  if(bad==6)x.put(IntelFixture::I+0x12afe + 4,0,1);
  if(bad==7)x.put(IntelFixture::S,14,1);
  if(bad==8)x.put(Fixture::X,Fixture::M+0x80570);
  if(bad==9)x.put(Fixture::M+40,0,1);
  if(bad==10)x.put(Fixture::A+0x118,0,4);
  if(bad==11)x.put(Fixture::A+0xc8,4096);
  assert(!x.beginReceipt(z,Fixture::G+4096,4)&&!x.endReceipt(z)&&z.q[Status]!=DeclaredSpan);
 }
 {IntelFixture x;Receipt z;assert(x.beginReceipt(z));x.put(Fixture::A+0x118,2U<<17,4);assert(!x.endReceipt(z)&&z.q[Status]==Changed);}
 {IntelFixture x;Receipt z;assert(!x.beginReceipt(z,Fixture::G+8192-4,8)&&z.q[BeforeStatus]==RangeOverrun);}
 printf("PASS Intel subclass: actual class/import/code guards, extra pages separate from memory/reservation, %u read faults x2, mutation and range controls, no writes\n",total);
}

#endif
