#define TGL_RESOURCE_METADATA_FIXTURE_ONLY
#include "tgl_video_resource_metadata.cpp"
#undef TGL_RESOURCE_METADATA_FIXTURE_ONLY
#include "kern_tgl_video_submit_metadata.hpp"
struct SubmitFixture:ResourceFixture {
 static constexpr uint64_t RING=K+0xe00000,CTX=K+0xe01000,OWNER=K+0xe02000,
  TABLE=K+0xe03000,BUFFER=K+0xe04000,LRC=K+0xe10000,CPU=K+0xe20000;
 TGLVideoAudit::Record audit{};
 SubmitFixture() {
  put(RING+0x20,CTX);put(RING+0x10,OWNER);put(RING+0x18,LRC);put(RING+0x40,3,1);
  put(CTX+0x50,OWNER);put(CTX+0xb0,RING);put(CTX+0x58,T);put(CTX+0x98,BUFFER);put(CTX+0x7c,7,4);
  put(T+0x260,TABLE);put(TABLE,I+0xcc230);put(BUFFER+0x38,LRC);put(BUFFER+0x20,8192);
  put(LRC+0x10c4,0,4);put(LRC+0x10cc,0x1000,4);
  const uint32_t command[]={0x70800000,0x82};bytes(CPU,reinterpret_cast<const uint8_t*>(command),8);
  auto q=audit.q;q[TGLVideoAudit::Schema]=4;q[TGLVideoAudit::Engine]=3;q[TGLVideoAudit::Ring]=RING;
  q[TGLVideoAudit::Context]=CTX;q[TGLVideoAudit::ContextID]=7;q[TGLVideoAudit::Stamp]=1;
  q[TGLVideoAudit::GPU]=G;q[TGLVideoAudit::Words]=2;q[TGLVideoAudit::CopiedBefore]=8;q[TGLVideoAudit::CopiedAfter]=8;
  q[TGLVideoAudit::Category]=2;q[TGLVideoAudit::ParseStop]=8;
  memcpy(audit.before,command,8);q[TGLVideoAudit::HashBefore]=q[TGLVideoAudit::HashAfter]=TGLVideoAudit::hash(audit.before,8);
  auto a=audit.alias.q;a[TGLVideoAlias::Schema]=1;a[TGLVideoAlias::Ring]=RING;a[TGLVideoAlias::Engine]=3;
  a[TGLVideoAlias::GPU]=G;a[TGLVideoAlias::CPU]=CPU;a[TGLVideoAlias::Bytes]=8;
  a[TGLVideoAlias::BeforeQualified]=1;a[TGLVideoAlias::Finished]=1;a[TGLVideoAlias::ExpectedClass]=I+0xcc230;
  auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};TGLVideoAlias::Reason reason;
  assert(TGLVideoAlias::ownership(audit.alias.owner[0],RING,3,I+0xcc230,rd,reason));
  memcpy(audit.alias.owner[1],audit.alias.owner[0],sizeof(audit.alias.owner[0]));reads=0;
 }
 void run(TGLVideoSubmitMetadata::Capture &c) {
  auto rd=[&](uint64_t p,void *d,unsigned n){return read(p,d,n);};
  TGLVideoSubmitMetadata::collect(c,audit,rd);
 }
};
static bool complete(const TGLVideoSubmitMetadata::Capture &c) {
 return c.q[TGLVideoSubmitMetadata::Guard]==TGLVideoSubmitMetadata::Observed &&
  !c.metadata.q[1] && c.metadata.samples[0].q[0]==TGLVideoResourceMetadata::Complete &&
  c.metadata.samples[1].q[0]==TGLVideoResourceMetadata::Complete;
}
#ifndef TGL_SUBMIT_METADATA_FIXTURE_ONLY
int main(int argc,char **argv) {
 SubmitFixture f;TGLVideoSubmitMetadata::Capture c;const auto original=f.memory;
 assert(TGLVideoSubmitMetadata::eligible(f.audit));f.run(c);
 assert(complete(c)&&f.memory==original&&!memcmp(&c.audit,&f.audit,sizeof(f.audit)));
 const unsigned reads=f.reads;
 for(bool partial:{false,true})for(unsigned at=1;at<=reads;++at) {
  f.memory=original;f.reads=0;f.failAt=at;f.shortRead=partial;f.run(c);
  assert(!complete(c)&&f.memory==original);
 }
 f.failAt=0;f.shortRead=false;
 // A failed outer command/owner sample must retain unknown, never Observed.
 for(unsigned bad=0;bad<4;++bad) {
  f.memory=original;
  if(bad==0)f.put(SubmitFixture::CPU,0x7f,1);
  if(bad==1)f.put(SubmitFixture::CTX+0x7c,8,4);
  if(bad==2)f.put(SubmitFixture::LRC+0x10cc,0x2000,4);
  if(bad==3)f.put(SubmitFixture::TABLE,0);
  const auto saved=f.memory;f.run(c);assert(!complete(c)&&f.memory==saved&&c.q[TGLVideoSubmitMetadata::Guard]!=0);
 }
 // Every input gate independently refuses incomplete or mismatched receipts.
 for(unsigned bad=0;bad<17;++bad) {
  SubmitFixture x;
  switch(bad) {
   case 0:x.audit.q[TGLVideoAudit::Schema]=3;break;
   case 1:x.audit.q[TGLVideoAudit::OwnerGuard]=1;break;
   case 2:x.audit.q[TGLVideoAudit::Engine]=0;break;
   case 3:x.audit.q[TGLVideoAudit::ContextID]=0;break;
   case 4:x.audit.q[TGLVideoAudit::Stamp]=0;break;
   case 5:x.audit.q[TGLVideoAudit::Words]=1025;break;
   case 6:x.audit.q[TGLVideoAudit::CopiedBefore]=4;break;
   case 7:x.audit.q[TGLVideoAudit::CopiedAfter]=4;break;
   case 8:x.audit.q[TGLVideoAudit::Category]=1;break;
   case 9:x.audit.q[TGLVideoAudit::ParseStop]=0;break;
   case 10:x.audit.alias.q[TGLVideoAlias::Status]=TGLVideoAlias::NotChecked;break;
   case 11:x.audit.alias.q[TGLVideoAlias::BeforeQualified]=0;break;
   case 12:x.audit.alias.q[TGLVideoAlias::Finished]=0;break;
   case 13:x.audit.alias.q[TGLVideoAlias::Ring]+=8;break;
   case 14:x.audit.alias.q[TGLVideoAlias::GPU]+=8;break;
   case 15:x.audit.alias.owner[1][TGLVideoAlias::ContextID]=8;break;
   case 16:x.audit.alias.owner[0][TGLVideoAlias::Task]+=8;break;
  }
  auto before=x.memory;assert(!TGLVideoSubmitMetadata::eligible(x.audit));x.run(c);
  assert(c.q[TGLVideoSubmitMetadata::Guard]==TGLVideoSubmitMetadata::NotChecked&&x.reads==0&&x.memory==before);
 }
 // A command change after the first observation must be detected at the end.
 {SubmitFixture x;unsigned commandReads=0;
  auto rd=[&](uint64_t p,void *d,unsigned n){auto got=x.read(p,d,n);if(p==SubmitFixture::CPU&&++commandReads==1)x.put(SubmitFixture::CPU,0x7f,1);return got;};
  TGLVideoSubmitMetadata::collect(c,x.audit,rd);assert(commandReads==2&&c.q[TGLVideoSubmitMetadata::Guard]==TGLVideoSubmitMetadata::CommandChanged);}
 f.memory=original;f.reads=0;f.run(c);assert(complete(c));
 if(argc==2){auto fp=fopen(argv[1],"wb");assert(fp&&fwrite(&c,sizeof(c),1,fp)==1&&!fclose(fp));}
 printf("PASS submit metadata: %u reads x2 fault modes, 17 admission negatives, outer drift and command race, no object writes\n",reads);
}
#endif
