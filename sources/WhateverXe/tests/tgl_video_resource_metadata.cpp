#define TGL_INTEL_MAP_FIXTURE_ONLY
#include "tgl_video_intel_map.cpp"
#undef TGL_INTEL_MAP_FIXTURE_ONLY
struct Bounds {}; // IOGraphicsTypes defines this name in kernel builds.
#include "kern_tgl_video_resource_metadata.hpp"
#include <type_traits>
using namespace TGLVideoResourceMetadata;
static_assert(!std::is_convertible<Capture,TGLVideoMap::Receipt>::value,"metadata is never an admission receipt");
struct ResourceFixture:IntelFixture {
 static constexpr uint64_t ARRAY=K+0xb00000,R1=K+0xc00000,R2=K+0xc00100;
 ResourceFixture() {
  const uint8_t code[]={0x8b,0x47,0x0c,0x48,0x8b,0x8f,0xb0,0,0,0,0x48,0x85,0xc9,0x74,0x21,0x55,
   0x48,0x89,0xe5,0x48,0x8b,0x97,0xa0,0,0,0,0x31,0xf6,0x48,0x8b,0x3c,0xf2,
   0x83,0x7f,0x28,1,0x83,0xd8,0xff,0x48,0xff,0xc6,0x48,0x39,0xf1,0x75,0xed,0x5d,0xc3};
  bytes(M+0x52a20,code,sizeof(code));
  put(A+0xc8,0);put(A+0xc,0,4);put(A+0xb0,2);put(A+0xa8,2);put(A+0xa0,ARRAY);
  put(ARRAY,R1);put(ARRAY+8,R2);put(R1,M+0x80000);put(R2,M+0x80008);
  put(R1+0x28,2,4);put(R2+0x28,0,4);
 }
 void capture(TGLVideoResourceMetadata::Snapshot &s) {auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};TGLVideoResourceMetadata::sample(s,T,rd);}
};
#ifndef TGL_RESOURCE_METADATA_FIXTURE_ONLY
int main(int argc,char **argv) {
 ResourceFixture f;TGLVideoResourceMetadata::Snapshot s;const auto original=f.memory;f.capture(s);
 assert(s.q[0]==Complete && s.q[2]==2 && s.q[5]==2 && s.nodes[0].q[2]==1);
 assert(s.nodes[0].fields[AssignedLength]==0 && s.nodes[0].fields[SelfPrepareCount]==0);
 assert(s.nodes[0].resources[0][2]==2 && s.nodes[0].resources[1][2]==0 && f.memory==original);
 const auto reads=f.reads;
 for(bool partial:{false,true})for(unsigned fail=1;fail<=reads;++fail) {
  f.memory=original;f.reads=0;f.failAt=fail;f.shortRead=partial;f.capture(s);
  assert(s.q[0]!=Complete && f.memory==original);
 }
 f.failAt=0;f.shortRead=false;
 for(unsigned bad=0;bad<15;++bad) {
  f.memory=original;
  if(bad==0)f.put(ResourceFixture::M+0x52a20,0,1);
  if(bad==1)f.put(ResourceFixture::T+0x210,33,4);
  if(bad==2)f.put(ResourceFixture::A+0xb0,9);
  if(bad==3)f.put(ResourceFixture::A+0xa8,1);
  if(bad==4)f.put(ResourceFixture::A+0xa0,1);
  if(bad==5)f.put(ResourceFixture::ARRAY,1);
  if(bad==6)f.put(ResourceFixture::R1,1);
  if(bad==7)f.put(ResourceFixture::A+0x90,ResourceFixture::T+8);
  if(bad==8)f.put(ResourceFixture::A+0x28,ResourceFixture::B);
  if(bad==9)f.put(ResourceFixture::B+0x30,ResourceFixture::A);
  if(bad==10)f.put(ResourceFixture::Y,ResourceFixture::M+0x80578);
  if(bad==11)f.put(ResourceFixture::A+0x10,0x40,4);
  if(bad==12)f.put(ResourceFixture::A+0x98,(1ULL<<47)-4096);
  if(bad==13)f.put(ResourceFixture::M+40,0,1);
  if(bad==14)f.put(ResourceFixture::T+0x1e8,ResourceFixture::A);
  const auto saved=f.memory;f.capture(s);assert(s.q[0]!=Complete && f.memory==saved);
 }
 f.memory=original;f.put(ResourceFixture::A+0xc,0xffffffff,4);f.capture(s);
 assert(s.q[0]==Complete && s.nodes[0].q[2]==0); // exact uint32 native getter wrap
 f.memory=original;f.changeList=true;f.capture(s);assert(s.q[0]==Changed);f.changeList=false;
 f.memory=original;Capture capture;capture.q[0]=1;capture.q[2]=7;capture.q[3]=ResourceFixture::T;
 capture.q[4]=0x1000;capture.q[5]=TGLVideoResourceMetadata::MaxMaps;capture.q[6]=MaxResources;capture.q[7]=2;
 f.capture(capture.samples[0]);f.capture(capture.samples[1]);
 assert(capture.samples[0].q[4]==capture.samples[1].q[4] && f.memory==original);
 TGLVideoMap::Receipt strict;assert(!f.beginReceipt(strict) && strict.q[BeforeStatus]==MapBounds);
 if(argc==2){auto fp=fopen(argv[1],"wb");assert(fp && fwrite(&capture,sizeof(capture),1,fp)==1 && !fclose(fp));}
 printf("PASS resource metadata: %u reads x2 fault modes, 15 negative cases, aggregate wrap, list changes, strict admission unchanged, no writes\n",reads);
}
#endif
