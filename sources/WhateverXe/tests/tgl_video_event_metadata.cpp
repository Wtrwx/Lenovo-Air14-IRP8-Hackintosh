#define TGL_SUBMIT_METADATA_FIXTURE_ONLY
#include "tgl_video_submit_metadata.cpp"
#undef TGL_SUBMIT_METADATA_FIXTURE_ONLY
struct EventFixture:SubmitFixture {
 static constexpr uint64_t ACC=K+0x500000,EM=K+0x1200000;
 TGLVideoResourceMetadata::Snapshot source{};
 EventFixture() {
  put(B,I+0xccf08);put(B+0x118,0,4);put(Y,I+0xcd6e8);
  zero(ACC+0x380,8);put(ACC+0x380,EM);put(ACC+0xdc8,0,4);
  zero(EM,0x40);put(EM,I+0xcea08);put(EM+0x10,ACC);put(EM+0x30,8,4);
  put(EM+0x28,1); // Deliberately invalid GPU stamp-pointer array: never followed.
  for(auto address:{A,B})for(unsigned i=0;i<8;++i)put(address+0x38+8*i,UINT32_MAX);
  put(A+0x38,(1ULL<<32)|3);put(EM+0xf8+24*3,0,4);put(EM+0xfc+24*3,1,4);
  for(auto address:{R1,R2}) {
   zero(address,0xa4);put(address,I+0xd93e0);put(address+0x15,1,1);put(address+0x16,2,1);
   put(address+0xa0,1,4);put(address+0x38,K+0x1400000);
  }
  put(R1+0x28,2,4);
  const unsigned slots[]={0x148,0x180,0x190,0x1d0};const uint64_t targets[]={0x307f6,0x30414,0x309a2,0x3123c};
  for(unsigned i=0;i<4;++i)put(I+0xcea08+slots[i],M+targets[i]);
  const uint8_t code0[]={0x55,0x48,0x89,0xe5,0x31,0xc0,0x48,0x8b,0x14,0xc6,0x83,0xfa,0xff,0x74,0x4b,0x48,0x89,0xd1,0x48,0xc1,0xe9,0x20,0x48,0x63,0xd2,0x4c,0x8d,0x04,0xd5,0x00,0x00,0x00,0x00,0x4f,0x8d,0x04,0x40,0x41,0x89,0xc9,0x46,0x2b,0x8c,0x07,0xf8,0x00,0x00,0x00,0x45,0x85,0xc9,0x7e,0x25,0x4c,0x8b,0x4f,0x28,0x49,0x8b,0x14,0xd1,0x8b,0x12,0x42,0x89,0x94,0x07,0xf8,0x00,0x00,0x00,0x29,0xd1,0x85,0xc9,0x7e,0x0d,0x48,0x8b,0x4f,0x10,0x83,0xb9,0xc8,0x0d,0x00,0x00,0x00,0x74,0x0d,0x48,0xff,0xc0,0x48,0x83,0xf8,0x08,0x75,0xa3,0xb0,0x01,0x5d,0xc3,0x31,0xc0,0xeb,0xfa};bytes(M+0x30414,code0,sizeof(code0));
  const uint8_t code1[]={0x55,0x48,0x89,0xe5,0x8b,0x47,0x30,0x5d,0xc3};bytes(M+0x1294a,code1,sizeof(code1));
  const uint8_t code2[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x87,0x88,0x00,0x00,0x00,0x48,0x8b,0x80,0x80,0x03,0x00,0x00,0x48,0x8d,0x77,0x38,0x48,0x8b,0x08,0x48,0x8b,0x89,0x90,0x01,0x00,0x00,0x48,0x89,0xc7,0x5d,0xff,0xe1};bytes(M+0x52462,code2,sizeof(code2));
  const uint8_t code3[]={0x48,0x63,0xce,0x48,0x8d,0x0c,0x49,0x8b,0x8c,0xcf,0xfc,0x00,0x00,0x00,0x48,0xc1,0xe1,0x20,0x89,0xf6,0x48,0x09,0xce,0x48,0x89,0x34,0xc2,0x5d,0xc3};bytes(M+0x312bc,code3,sizeof(code3));
  auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};TGLVideoResourceMetadata::sample(source,T,rd);
  assert(source.q[0]==TGLVideoResourceMetadata::Complete && source.q[2]==2);reads=0;
 }
 uint64_t eventRead(uint64_t p,void *d,unsigned n) {
  assert(!(p<=EM+0x28 && EM+0x28<p+n)); // no GPU/shared stamp-pointer access
  assert(p!=K+0x1400000);return read(p,d,n); // no resource event payload access
 }
 void run(TGLVideoEventMetadata::Snapshot &s){auto rd=[&](uint64_t p,void *d,unsigned n){return eventRead(p,d,n);};TGLVideoEventMetadata::sample(s,source,T,rd);}
 void submit(TGLVideoSubmitMetadata::Capture &c,TGLVideoEventMetadata::Capture &e){auto rd=[&](uint64_t p,void *d,unsigned n){return eventRead(p,d,n);};TGLVideoSubmitMetadata::collect(c,audit,rd,&e);}
};
#ifndef TGL_EVENT_METADATA_FIXTURE_ONLY
int main(int argc,char **argv) {
 using namespace TGLVideoEventMetadata;
 EventFixture f;TGLVideoEventMetadata::Snapshot s;const auto original=f.memory;f.run(s);
 if(s.q[TGLVideoEventMetadata::Status]!=TGLVideoEventMetadata::Complete)fprintf(stderr,"event sample status=%llu reads=%llu visited=%llu\n",s.q[0],s.q[1],s.q[2]);
 assert(s.q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Complete && s.q[Visited]==2 && f.memory==original);
 assert(s.nodes[0].event[0]==((1ULL<<32)|3) && !s.nodes[0].cached[0][0] && s.nodes[0].cached[0][1]==1);
 assert(s.nodes[0].resources[0][3]==0 && s.nodes[0].resources[0][6]==1 && s.nodes[1].event[0]==UINT32_MAX);
 const unsigned reads=f.reads;
 for(bool partial:{false,true})for(unsigned fail=1;fail<=reads;++fail) {
  f.memory=original;f.reads=0;f.failAt=fail;f.shortRead=partial;f.run(s);
  assert(s.q[TGLVideoEventMetadata::Status]!=TGLVideoEventMetadata::Complete && f.memory==original);
 }
 f.failAt=0;f.shortRead=false;
 for(unsigned bad=0;bad<20;++bad) {
  f.memory=original;auto source=f.source;
  if(bad==0)f.put(EventFixture::EM,EventFixture::I+0xcea10);
  if(bad==1)f.put(EventFixture::I+0xcea08+0x180,EventFixture::M+0x309a2);
  if(bad==2)f.put(EventFixture::M+0x30414+48,0,1);
  if(bad==3)f.put(EventFixture::EM+0x30,129,4);
  if(bad==4)f.put(EventFixture::EM+0x30,0,4);
  if(bad==5)f.put(EventFixture::EM+0x10,1);
  if(bad==6)f.put(EventFixture::A+0x38,(1ULL<<32)|8);
  if(bad==7)f.put(EventFixture::A+0x38,(1ULL<<32)|0xfffffffe);
  if(bad==8)f.put(EventFixture::R1,EventFixture::I+0xd93e8);
  if(bad==9)f.put(EventFixture::ARRAY,EventFixture::R2);
  if(bad==10)f.put(EventFixture::R1+0x28,3,4);
  if(bad==11)f.put(EventFixture::A+0xc,5,4);
  if(bad==12)f.put(EventFixture::A+0x90,EventFixture::T+8);
  if(bad==13)f.put(EventFixture::T+0x210,2,4);
  if(bad==14)f.source.q[0]=TGLVideoMap::ReadFailed;
  if(bad==15)f.source.q[2]=33;
  if(bad==16)f.source.nodes[1].fields[TGLVideoMap::IntelModule]=0;
  if(bad==17)f.source.nodes[0].fields[TGLVideoMap::ResourceCount]=9;
  if(bad==18)f.put(EventFixture::I+40,0,1);
  if(bad==19)f.source.nodes[1].fields[TGLVideoMap::Address]=EventFixture::A;
  const auto before=f.memory;f.run(s);assert(s.q[TGLVideoEventMetadata::Status]!=TGLVideoEventMetadata::Complete && f.memory==before);f.source=source;
 }
 // Concurrent changes are observations, not false success or a reason to wait.
 for(unsigned race=0;race<6;++race) {
  EventFixture x;unsigned hit=0;
  auto rd=[&](uint64_t p,void *d,unsigned n){auto got=x.eventRead(p,d,n);
   const uint64_t trigger[]={EventFixture::A+0x38,EventFixture::EM+0xf8+24*3,EventFixture::ACC+0xdc8,EventFixture::R1+0x14,EventFixture::A+0x20,EventFixture::T+0x200};
   if(p==trigger[race] && ++hit==1) {
    if(race==0)x.put(p,(2ULL<<32)|3);
    if(race==1)x.put(p,1,4);
    if(race==2)x.put(p,1,4);
    if(race==3)x.put(p,1,1);
    if(race==4)x.put(p,1,4);
    if(race==5)x.put(p,0);
   }return got;};
  TGLVideoEventMetadata::sample(s,x.source,EventFixture::T,rd);
  // The last-header change occurs after its only read: cannot be detected by
  // sequential sampling. Treat this as an explicit limitation, not a lease.
  if(race==5)assert(s.q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Complete);
  else assert(s.q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Changed);
 }
 f.memory=original;f.reads=0;TGLVideoSubmitMetadata::Capture c;TGLVideoEventMetadata::Capture e;f.submit(c,e);
 assert(complete(c) && !e.q[Guard] && e.q[Stamp]==1 && e.q[CommandHash]==f.audit.q[TGLVideoAudit::HashAfter]);
 assert(e.samples[0].q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Complete && e.samples[1].q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Complete);
 assert(!memcmp(e.samples[0].nodes,e.samples[1].nodes,sizeof(e.samples[0].nodes)) && f.memory==original);
 f.put(EventFixture::CPU,0x7f,1);f.submit(c,e);assert(e.q[Guard] && e.samples[0].q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::NotChecked);
 f.memory=original;f.put(EventFixture::ACC+0xdc8,1,4);f.submit(c,e);
 assert(!e.q[Guard] && e.samples[0].q[OverrideBefore]==1 && e.samples[0].q[TGLVideoEventMetadata::Status]==TGLVideoEventMetadata::Complete);
 // Nonzero override is recorded, never called completion by this collector.
 f.memory=original;f.submit(c,e);
 if(argc==2){auto fp=fopen(argv[1],"wb");assert(fp && fwrite(&e,sizeof(e),1,fp)==1 && !fclose(fp));}
 printf("PASS event CPU metadata: %u reads x2 fault modes, 20 negatives, 5 detected races, explicit post-last-read limitation, paired submit samples, no GPU stamp access/writes/waits\n",reads);
}
#endif
