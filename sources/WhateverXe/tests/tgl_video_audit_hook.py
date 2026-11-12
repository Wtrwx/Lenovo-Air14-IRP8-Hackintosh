#!/usr/bin/env python3
"""Execute the production submit wrapper with bounded mock CPU mappings."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
start=source.index('// Capture CPU receipts only.')
stop=source.index('\nvoid XE::wrapTGLHangRingDump',start)
body=source[start:stop]
fixture=r'''
#include "kern_tgl_video_audit.hpp"
#include "kern_tgl_video_surface.hpp"
#include <vector>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cstdio>
struct Range {uint64_t p,n;};static std::vector<Range> ranges;
static void add(void *p,size_t n){ranges.push_back({uint64_t(p),n});}
namespace TGLHangPending {bool kernelRange(uint64_t p,uint64_t n){for(auto r:ranges)if(p>=r.p && p-r.p<=r.n && n<=r.n-(p-r.p))return true;return false;}}
static bool failRead;
static uint32_t *raceSource;static size_t raceWords;static unsigned raceReads;
static uint64_t nofault(uint64_t p,uint64_t out,uint64_t n){if(failRead || !TGLHangPending::kernelRange(p,n))return 0;memcpy((void*)out,(void*)p,n);if(p==uint64_t(raceSource)&&n==raceWords*4&&++raceReads==2)raceSource[raceWords-1]^=1;return n;}
static struct {uint64_t(*nofault)(uint64_t,uint64_t,uint64_t)=::nofault;}tglVM;
static bool tglVMRead(uint64_t p,uint64_t &out){return nofault(p,uint64_t(&out),8)==8;}
static bool failAllocation;
static void *IOMalloc(size_t n){if(failAllocation)return nullptr;auto p=malloc(n);if(p)add(p,n);return p;}
static void IOFree(void *p,size_t){ranges.erase(std::remove_if(ranges.begin(),ranges.end(),[&](Range r){return r.p==uint64_t(p);}),ranges.end());free(p);}
static uint64_t clockCount;static void tglHostClock(uint64_t *out){*out=++clockCount;}
static bool tglVideoAuditEnabled,tglVideoProgressiveEnabled,tglVideoSurfaceEnabled,tglVideoSixDWORDEnabled;
static TGLVideoAudit::History<> tglVideoAudit,tglVideoVCSAudit;static uint64_t tglVideoAuditSkipped;
static void tglCaptureVideoSubmitMetadata(const TGLVideoAudit::Record &){}
// Mapping traversal is exercised with real page-table fixtures by tgl_video_alias.
// Here its result is injected to exercise the production command-edit gate.
static bool aliasPermitted=true;
static void tglVideoAliasBegin(TGLVideoAudit::Record &r,const uint32_t*){r.alias.q[TGLVideoAlias::Schema]=1;r.alias.q[TGLVideoAlias::BeforeQualified]=1;}
static bool tglVideoAliasFinish(TGLVideoAudit::Record &r){r.alias.q[TGLVideoAlias::Status]=aliasPermitted?TGLVideoAlias::Ready:TGLVideoAlias::AliasMismatch;++r.alias.q[TGLVideoAlias::Finished];return aliasPermitted;}
// Actual bounded traversal, UUID/slot checks and failures use tgl_video_map.
// Here observe call order and ensure diagnostics cannot gate native delegation.
static unsigned mapBegins,mapFinishes;
static void tglVideoMapBegin(TGLVideoAudit::Record &r){++mapBegins;r.map.q[TGLVideoMap::Schema]=1;r.map.q[TGLVideoMap::Status]=TGLVideoMap::NotChecked;}
static void tglVideoMapFinish(TGLVideoAudit::Record &r){++mapFinishes;r.map.q[TGLVideoMap::Status]=TGLVideoMap::ReadFailed;}
struct Registry {unsigned updates=0;void setProperty(const char*,uint32_t n,unsigned bits){assert(n==4&&bits==32);++updates;}};static Registry registry;
struct IORegistryEntry {static Registry *getRegistryRoot(){return &registry;}};
static unsigned nativeCalls;static uint32_t expectedStamp;static bool expectedA,expectedB;
static bool nativeSubmit(void *ring,uint64_t gpu,uint32_t stamp,uint32_t *cpu,uint32_t words,bool a,bool b){assert(ring&&gpu==0x40010000&&stamp==expectedStamp&&cpu&&words&&a==expectedA&&b==expectedB);++nativeCalls;return false;}
#define FunctionCast(...) nativeSubmit
struct XE {static bool wrapTGLVideoBatch(void*,uint64_t,uint32_t,uint32_t*,uint32_t,bool,bool);};
// PRODUCTION_WRAPPER
static std::vector<uint32_t> broken(){
 std::vector<uint32_t> v;auto packet=[&](uint32_t h,unsigned n){unsigned i=v.size();v.resize(i+n);v[i]=h;return i;};
 auto i=packet(0x70000003,5);v[i+1]=0x12;i=packet(0x70800000,2);v[i+1]=0x82;i=packet(0x71000013,21);v[i+1]=108*68;v[i+2]=(67<<16)|107;v[i+4]=6;
 for(unsigned n=0;n<4;n++){i=packet(0x71030009,11);v[i+4]=(n*34<<24)|(n*34*108);v[i+5]=(n==3?68:(n+1)*34)<<16;v[i+6]=n==3?1<<19:0;auto w=packet(0x70870002,4);v[w+1]=n*17;v[w+2]=(n+1)*17;}return v;
}
static std::vector<uint32_t> surfaceBatch(){
 auto v=broken();TGLVideoProgressive::Plan p;assert(TGLVideoProgressive::plan(v.data(),v.size(),p));assert(TGLVideoProgressive::apply(v.data(),v.size(),p));
 std::vector<uint32_t> states;
 for(unsigned s=0;s<6;s++){unsigned t=s%3;states.push_back(s<3?0x70010004:0x70810004+(t<<16));states.push_back(s<3?(t==0?4:t==1?0:5):0);states.push_back(t==2?0x043c1af0:0x10fc6bf0);states.push_back(t==2?0x48000ffb:0x480037fb);states.push_back(t==2?288:1088);states.push_back(t==2?288:1088);}
 v.insert(v.begin()+7,states.begin(),states.end());return v;
}
static std::vector<uint32_t> brokenSurfaces(bool extended){
 auto v=surfaceBatch();
 for(unsigned i=0;i<v.size();i++)if(v[i]==0x71000013)v[i+4]|=2;else if(v[i]==0x71030009){unsigned start=v[i+4]>>24,end=v[i+5]>>16;v[i+4]=(start*2<<24)|(start*2*108);if(end!=68)v[i+5]=end*2<<16;}
 if(extended){assert(v[5]==0x70800000);v[5]=0x70800004;v.insert(v.begin()+7,{0x002b030a,0x07000303,0,0});}
 return v;
}
int main(){
 std::vector<uint8_t> ring(0x900),context(0x100);add(ring.data(),ring.size());add(context.data(),context.size());
 uint64_t p=uint64_t(context.data());memcpy(ring.data()+0x20,&p,8);p=uint64_t(ring.data());memcpy(context.data()+0xb0,&p,8);uint32_t id=241;memcpy(context.data()+0x7c,&id,4);
 for(bool audit:{false,true})for(bool fix:{false,true})for(unsigned engine:{0U,3U}) {
  memset(&tglVideoAudit,0,sizeof(tglVideoAudit));registry.updates=0;tglVideoAuditEnabled=audit;tglVideoProgressiveEnabled=fix;ring[0x40]=engine;auto v=broken(),before=v;add(v.data(),v.size()*4);expectedStamp=33;expectedA=true;expectedB=false;unsigned old=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(nativeCalls==old+1);assert(registry.updates==(fix&&engine==3));assert((v!=before)==(fix&&engine==3));assert(tglVideoAudit.attempts==unsigned(audit));
  if(audit){TGLVideoAudit::Record record;assert(tglVideoAudit.copy(tglVideoAudit.early[0],record)==1);assert(record.q[TGLVideoAudit::ContextID]==241 && record.q[TGLVideoAudit::OwnerGuard]==0 && record.q[TGLVideoAudit::Stamp]==33);assert(record.q[TGLVideoAudit::HashBefore]==TGLVideoAudit::hash((uint8_t*)before.data(),before.size()*4));assert(record.q[TGLVideoAudit::HashAfter]==TGLVideoAudit::hash((uint8_t*)v.data(),v.size()*4));assert(record.q[TGLVideoAudit::Disposition]==unsigned(fix&&engine==3?TGLVideoAudit::Applied:TGLVideoAudit::ObserveOnly));}
  ranges.pop_back();
 }
 // Surface mode independently preserves native delegation and journal identity.
 for(bool audit:{false,true})for(bool surface:{false,true})for(unsigned engine:{0U,3U}) {
  memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoAuditEnabled=audit;tglVideoSurfaceEnabled=surface;tglVideoProgressiveEnabled=true;ring[0x40]=engine;
  auto v=surfaceBatch(),before=v;add(v.data(),v.size()*4);unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(nativeCalls==n+1);
  const bool edited=audit&&surface&&engine==3;assert((v!=before)==edited);
  if(audit){TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);assert(r.q[TGLVideoAudit::Edits]==unsigned(edited?3:0));assert(r.q[TGLVideoAudit::HashAfter]==TGLVideoAudit::hash((uint8_t*)v.data(),v.size()*4));if(edited){assert(r.q[TGLVideoAudit::SurfaceDisposition]==TGLVideoAudit::Applied);auto rebuilt=before;for(unsigned i=0;i<r.q[TGLVideoAudit::Edits];i++){auto e=r.edits[i];assert(rebuilt[e.index]==e.before);rebuilt[e.index]=e.after;}assert(rebuilt==v);}}
  ranges.pop_back();
 }
 // An unqualified context or partial diagnostic copy must never enable edits.
 for(unsigned failure:{0U,1U,2U}) {
  memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoAuditEnabled=tglVideoSurfaceEnabled=true;tglVideoProgressiveEnabled=false;ring[0x40]=3;
  auto v=surfaceBatch(),before=v;add(v.data(),v.size()*4);if(failure==0){uint64_t zero=0;memcpy(context.data()+0xb0,&zero,8);}failRead=failure==1;failAllocation=failure==2;unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(nativeCalls==n+1&&v==before);
  p=uint64_t(ring.data());memcpy(context.data()+0xb0,&p,8);failRead=failAllocation=false;ranges.pop_back();
 }
 tglVideoSurfaceEnabled=false;
 // An otherwise editable command must remain native when its address check fails.
 {memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoAuditEnabled=tglVideoSurfaceEnabled=true;tglVideoProgressiveEnabled=false;ring[0x40]=3;
  auto v=surfaceBatch(),before=v;add(v.data(),v.size()*4);aliasPermitted=false;unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(nativeCalls==n+1&&v==before);
  TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);assert(r.q[TGLVideoAudit::SurfaceDisposition]==TGLVideoAudit::MappingRejected&&r.q[TGLVideoAudit::Edits]==0);
  assert(r.alias.q[TGLVideoAlias::AtApplyAttempted]&&r.alias.q[TGLVideoAlias::AtApplyStatus]==TGLVideoAlias::AliasMismatch);aliasPermitted=true;ranges.pop_back();}
 tglVideoSurfaceEnabled=false;
 // A combined receipt must reconstruct both independent edit groups exactly.
 {memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoAuditEnabled=tglVideoSurfaceEnabled=tglVideoProgressiveEnabled=true;ring[0x40]=3;auto v=surfaceBatch();
  for(unsigned i=0;i<v.size();i++)if(v[i]==0x71000013)v[i+4]|=2;else if(v[i]==0x71030009){unsigned start=v[i+4]>>24,end=v[i+5]>>16;v[i+4]=(start*2<<24)|(start*2*108);if(end!=68)v[i+5]=end*2<<16;}
  auto before=v;add(v.data(),v.size()*4);assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);assert(r.q[TGLVideoAudit::Edits]==12&&r.q[TGLVideoAudit::Disposition]==TGLVideoAudit::Applied&&r.q[TGLVideoAudit::SurfaceDisposition]==TGLVideoAudit::Applied);for(unsigned i=0;i<12;i++){auto e=r.edits[i];assert(before[e.index]==e.before);before[e.index]=e.after;}assert(before==v);ranges.pop_back();}
 // The real production wrapper selects one exact layout and delegates once.
 for(bool enabled:{false,true})for(bool extended:{false,true})for(unsigned engine:{0U,3U}) {
  memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoSixDWORDEnabled=enabled;
  tglVideoAuditEnabled=tglVideoSurfaceEnabled=tglVideoProgressiveEnabled=true;ring[0x40]=engine;
  auto v=brokenSurfaces(extended),before=v;add(v.data(),v.size()*4);unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
  const bool applied=enabled==extended&&engine==3;assert(nativeCalls==n+1&&(v!=before)==applied);
  TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);
  assert(r.q[TGLVideoAudit::Edits]==unsigned(applied?12:0));
  if(applied){for(unsigned i=0;i<12;i++){auto e=r.edits[i];assert(before[e.index]==e.before);before[e.index]=e.after;}assert(before==v);}
  ranges.pop_back();
 }
 // Long-layout edits require a complete receipt, owner and address check.
 for(unsigned failure:{0U,1U,2U,3U}) {
  memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoSixDWORDEnabled=true;
  tglVideoAuditEnabled=failure!=0;tglVideoSurfaceEnabled=tglVideoProgressiveEnabled=true;ring[0x40]=3;
  auto v=brokenSurfaces(true);if(failure==2)v.resize(1050);auto before=v;add(v.data(),v.size()*4);
  if(failure==1){uint64_t zero=0;memcpy(context.data()+0xb0,&zero,8);}aliasPermitted=failure!=3;unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
  assert(v==before&&nativeCalls==n+1);p=uint64_t(ring.data());memcpy(context.data()+0xb0,&p,8);aliasPermitted=true;ranges.pop_back();
 }
 // A non-edit command word changed after copying must prevent all edits.
 {memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoSixDWORDEnabled=true;
  tglVideoAuditEnabled=tglVideoSurfaceEnabled=tglVideoProgressiveEnabled=true;ring[0x40]=3;
  auto v=brokenSurfaces(true);v.push_back(0);auto expected=v;expected.back()=1;add(v.data(),v.size()*4);
  raceSource=v.data();raceWords=v.size();raceReads=0;unsigned n=nativeCalls;
  assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
  assert(v==expected&&nativeCalls==n+1&&raceReads>=2);TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);
  assert(r.q[TGLVideoAudit::Disposition]==TGLVideoAudit::ChangedBeforeApply);raceSource=nullptr;raceWords=0;ranges.pop_back();}
 tglVideoSixDWORDEnabled=false;
 // Restrict format edits to a completely captured command buffer.
 {tglVideoProgressiveEnabled=false;auto v=surfaceBatch();v.resize(1050);auto before=v;add(v.data(),v.size()*4);assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(v==before);ranges.pop_back();}
 tglVideoSurfaceEnabled=false;
 // An unknown command family must remain available for paired comparisons.
 memset(&tglVideoAudit,0,sizeof(tglVideoAudit));tglVideoAuditEnabled=true;tglVideoProgressiveEnabled=false;ring[0x40]=0;
 {std::vector<uint32_t> unknown{0xdeadbeef};add(unknown.data(),4);assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,unknown.data(),1,true,false));TGLVideoAudit::Record r;assert(tglVideoAudit.copy(tglVideoAudit.early[0],r)==1);assert(r.q[TGLVideoAudit::Category]==0 && r.q[TGLVideoAudit::ParseStop]==0 && (r.q[TGLVideoAudit::Flags]&8));ranges.pop_back();}
 // A failed diagnostic copy/allocation must still call native exactly once.
 tglVideoAuditEnabled=true;tglVideoProgressiveEnabled=false;ring[0x40]=3;auto v=broken();add(v.data(),v.size()*4);
 for(unsigned failure:{1U,2U}){unsigned n=nativeCalls;failRead=failure==1;failAllocation=failure==2;assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));assert(nativeCalls==n+1);failRead=failAllocation=false;}
 assert(mapBegins>0&&mapFinishes>=mapBegins);
 // Desktop submissions must not evict a video receipt before hang reporting.
 memset(&tglVideoAudit,0,sizeof(tglVideoAudit));memset(&tglVideoVCSAudit,0,sizeof(tglVideoVCSAudit));
 ring[0x40]=3;unsigned old=nativeCalls;
 assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
 assert(tglVideoVCSAudit.attempts==1);
 ring[0x40]=0;
 for(unsigned i=0;i<200;i++)assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
 assert(nativeCalls==old+201 && tglVideoAudit.attempts==201 && tglVideoVCSAudit.attempts==1);
 TGLVideoAudit::Record savedVideo;assert(tglVideoVCSAudit.copy(tglVideoVCSAudit.recent[0],savedVideo)==1);
 assert(savedVideo.q[TGLVideoAudit::Engine]==3 && savedVideo.q[TGLVideoAudit::ContextID]==241);
 tglVideoAudit.freeze();tglVideoVCSAudit.freeze();ring[0x40]=3;old=nativeCalls;
 assert(!XE::wrapTGLVideoBatch(ring.data(),0x40010000,expectedStamp,v.data(),v.size(),true,false));
 assert(nativeCalls==old+1 && tglVideoVCSAudit.attempts==1 && tglVideoAudit.attempts==201);
 puts("PASS production video submit wrapper: audit off/on, fix off/on, engines 0/3, context/stamp/hash, native return and exactly-once delegation, failed reads/allocations/address/map-metadata checks");
}
'''.replace('// PRODUCTION_WRAPPER',body)
with tempfile.TemporaryDirectory(prefix='wex-video-audit-hook-') as d:
 d=Path(d);(d/'test.cpp').write_text(fixture)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
