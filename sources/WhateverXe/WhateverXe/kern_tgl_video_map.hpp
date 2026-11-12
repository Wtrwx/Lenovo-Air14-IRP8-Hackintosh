// Read-only native map metadata samples. Declared spans and two equal samples
// are NOT allocation, generation, CPU alias, mapping lease or lifetime proof.
#ifndef KERN_TGL_VIDEO_MAP_HPP
#define KERN_TGL_VIDEO_MAP_HPP
#include <stdint.h>
#include <string.h>
namespace TGLVideoMap {
constexpr unsigned MaxMaps=128;
enum Reason {DeclaredSpan,NotChecked,Bounds,ReadFailed,DonorMismatch,ListLimit,
 ListShape,DuplicateMap,MapClass,MemoryClass,TaskMismatch,IndirectAddress,
 MapBounds,NotContained,Ambiguous,RangeOverrun,SelfNotPrepared,ResourceReferencesUnsupported,Changed};
enum Field {Schema,Status,Task,GPU,Bytes,BeforeStatus,AfterStatus,Finished,Fields};
enum NodeField {Address,Class,Flags,Memory,Previous,Next,OwnerTask,Accelerator,
 Base,AssignedLength,ResourceCount,SelfPrepareCount,MemClass,MemoryLength,LRUSeed,NativeMapLength,ExtraPages,PageShift,IntelModule,NodeFields};
// Native +0xb0 counts associated IOAccelResource2 entries, not submaps.
// +0xc is self prepare count; native getPrepareCount additionally aggregates
// nonzero +0x28 counts from resource pointers in the +0xa0 array.
// +0xc8 belongs to explicit VA reservations; normal allocation may leave zero.
// This strict subset still rejects these cases; rejection is not invalidity.
struct Snapshot {
 uint64_t q[8]{}; // reason, reads, visited, matches, module, digest, last map/class
 uint64_t lists[2][3]{}; // head, tail, count
 uint64_t node[NodeFields]{}; // containing map on success; last inspected node on rejection
};
struct Receipt {uint64_t q[Fields]{};Snapshot samples[2]{};};
static_assert(sizeof(Snapshot)==264 && sizeof(Receipt)==592,"map metadata schema 2");
inline bool kernelRange(uint64_t p,uint64_t n) {
 return n && p>>47==0x1ffff && p<=UINT64_MAX-(n-1) && (p+n-1)>>47==0x1ffff;
}
inline uint64_t hash(const void *p,unsigned bytes,uint64_t h) {
 const auto b=static_cast<const uint8_t *>(p);
 for(unsigned i=0;i<bytes;++i){h^=b[i];h*=1099511628211ULL;}return h;
}
template<class Read> struct Reader {
 Read &read;uint64_t &count;bool failed=false;
 bool operator()(uint64_t p,void *out,unsigned n) {
  ++count;const bool ok=kernelRange(p,n) && read(p,out,n)==n;if(!ok)failed=true;return ok;
 }
 bool get(uint64_t p,unsigned offset,uint64_t &out,unsigned n=8) {
  out=0;if(!kernelRange(p,uint64_t(offset)+n)){failed=true;return false;}return (*this)(p+offset,&out,n);
 }
};
// Exact IOAcceleratorFamily2 UUID, vtable slots and short getter signatures.
// Resolve from the observed map vptr; never invoke a virtual method or kext.
template<class Read> bool donor(uint64_t vptr,uint64_t &module,Reader<Read> &r) {
 constexpr uint64_t MapVptr=0x7ffe8,MemVptr=0x80570;
 if(vptr<MapVptr)return false;
 module=vptr-MapVptr;
 if(!kernelRange(module,0x806a0))return false;
 uint32_t header[8]{};
 if(!r(module,header,sizeof(header)) || header[0]!=0xfeedfacf || header[1]!=0x1000007 ||
    header[3]!=11 || header[4]!=9 || header[5]!=1488)return false;
 constexpr uint8_t uuid[16]={0x6c,0x4a,0x16,0xb1,0x66,0xdc,0x3a,0x8b,0x9f,0x0b,0xd3,0xe0,0x27,0xb1,0x6e,0xb1};
 unsigned at=32;bool found=false;
 for(unsigned i=0;i<header[4];++i) {
  uint32_t lc[2]{};
  if(at>32+header[5]-8 || !r(module+at,lc,8) || lc[1]<8 || lc[1]>32+header[5]-at)return false;
  if(lc[0]==0x1b) {
   uint8_t actual[16]{};
   if(found || lc[1]!=24 || !r(module+at+8,actual,16) || memcmp(actual,uuid,16))return false;
   found=true;
  }
  at+=lc[1];
 }
 if(!found || at!=32+header[5])return false;
 const unsigned offsets[]={MapVptr+0x128,MapVptr+0x168,MapVptr+0x190,MemVptr+0x120};
 const uint64_t targets[]={0x526ec,0x52bd4,0x52b50,0x5595a};
 for(unsigned i=0;i<4;++i){uint64_t value=0;if(!r.get(module,offsets[i],value) || value!=module+targets[i])return false;}
 const uint8_t gpu[]={0xf6,0x47,0x10,0x40,0x75,0x08,0x48,0x8b,0x87,0x98,0,0,0,0xc3};
 const uint8_t length[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x7f,0x18,0x48,0x8b,0x07,0x5d,0xff,0xa0,0x20,1,0,0};
 const uint8_t assigned[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x87,0xc8,0,0,0,0x5d,0xc3};
 const uint8_t memory[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x40,0x5d,0xc3};
 const uint8_t *expected[]={gpu,length,assigned,memory};
 const unsigned sizes[]={sizeof(gpu),sizeof(length),sizeof(assigned),sizeof(memory)};
 for(unsigned i=0;i<4;++i){uint8_t bytes[18]{};if(!r(module+targets[i],bytes,sizes[i]) || memcmp(bytes,expected[i],sizes[i]))return false;}
 return true;
}
// Exact Intel subclass ABI, qualified separately from the inherited provider.
// No virtual calls. Extra pages describe the native map getter, not allocations.
template<class Read> bool intelDonor(uint64_t vptr,uint64_t &provider,uint64_t &intel,uint64_t &shift,Reader<Read> &r) {
 if(vptr<0xccf08 || (vptr&4095)!=0xf08)return false;
 intel=vptr-0xccf08;if(!kernelRange(intel,0xcd810) || (intel&4095))return false;
 uint32_t h[8]{};if(!r(intel,h,32) || h[0]!=0xfeedfacf || h[1]!=0x1000007 || h[3]!=11 || !h[4] || h[4]>32 || h[5]>4096)return false;
 const uint8_t uuid[]={0xba,0x3a,0xa1,0xc0,0xfe,0x6b,0x33,0xb3,0x9d,0x85,0x73,0xf8,0x48,0x39,0x4e,0x3d};
 unsigned at=32;bool found=false;
 for(unsigned i=0;i<h[4];++i){uint32_t lc[2]{};if(at>32+h[5]-8 || !r(intel+at,lc,8) || lc[1]<8 || lc[1]>32+h[5]-at)return false;
  if(lc[0]==0x1b){uint8_t u[16]{};if(found || lc[1]!=24 || !r(intel+at+8,u,16) || memcmp(u,uuid,16))return false;found=true;}at+=lc[1];}
 if(!found || at!=32+h[5])return false;
 uint64_t super=0,shiftPointer=0;
 if(!r.get(intel,0xc8130,super) || !kernelRange(super,16) || !donor(super+16,provider,r) ||
    !r.get(intel,0xc8230,shiftPointer) || !r.get(shiftPointer,0,shift,1) || shift!=12)return false;
 const unsigned slots[]={0xcd030,0xcd070,0xcd098,0xcd808};
 const uint64_t targets[]={provider+0x526ec,intel+0x1118a,provider+0x52b50,intel+0x12afe};
 for(unsigned i=0;i<4;++i){uint64_t value=0;if(!r.get(intel,slots[i],value) || value!=targets[i])return false;}
 const uint8_t length[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x48,0x8b,0x05,0x96,0x6f,0x0b,0,0xff,0x90,0x78,1,0,0,0x8b,0x8b,0x18,1,0,0,0xc1,0xe9,0x11,0x0f,0xb6,0xd1,0x48,0x8b,0x0d,0x7d,0x70,0x0b,0,0x8a,0x09,0xd3,0xe2,0x48,0x63,0xca,0x48,0x01,0xc8,0x48,0x83,0xc4,8,0x5b,0x5d,0xc3};
 uint8_t code[sizeof(length)]{};
 if(!r(intel+0x1118a,code,32) || !r(intel+0x1118a+32,code+32,sizeof(code)-32) || memcmp(code,length,sizeof(code)))return false;
 const uint8_t memory[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x47,0x40,0x5d,0xc3};uint8_t actual[10]{};
 return r(intel+0x12afe,actual,10) && !memcmp(actual,memory,10);
}
namespace Detail {
template<class Read> bool sample(Snapshot &s,uint64_t task,uint64_t gpu,uint64_t bytes,Read read,bool observeOnly) {
 memset(&s,0,sizeof(s));s.q[0]=Bounds;s.q[5]=14695981039346656037ULL;
 if(!kernelRange(task,0x218) || !gpu || gpu>>47 || !bytes || bytes>(1ULL<<47)-gpu)return false;
 Reader<Read> r{read,s.q[1]};s.q[0]=ReadFailed;
 for(unsigned i=0;i<2;++i) {
  const unsigned offset=i?0x1e8:0x200;
  if(!r.get(task,offset,s.lists[i][0]) || !r.get(task,offset+8,s.lists[i][1]) ||
     !r.get(task,offset+16,s.lists[i][2],4))return false;
 }
 s.q[0]=ListLimit;
 if(s.lists[0][2]>MaxMaps || s.lists[1][2]>MaxMaps-s.lists[0][2])return false;
 uint64_t seen[MaxMaps]{};unsigned visited=0;
 for(unsigned i=0;i<2;++i) {
  auto address=s.lists[i][0];uint64_t previous=0;
  for(unsigned n=0;n<s.lists[i][2];++n) {
   s.q[0]=ListShape;if(!kernelRange(address,0xd0))return false;
   s.q[0]=DuplicateMap;for(unsigned j=0;j<visited;++j)if(seen[j]==address)return false;
   seen[visited++]=address;s.q[2]=visited;
   uint64_t node[NodeFields]{};node[Address]=address;s.q[0]=ReadFailed;
   if(!r.get(address,0,node[Class]))return false;
   s.q[6]=address;s.q[7]=node[Class];
   uint64_t provider=0,intel=0,shift=0;s.q[0]=DonorMismatch;
   const bool nativeIntel=(node[Class]&4095)==0xf08;
   const bool qualified=nativeIntel?intelDonor(node[Class],provider,intel,shift,r):donor(node[Class],provider,r);
   if(!qualified){if(r.failed)s.q[0]=ReadFailed;return false;}
   if(s.q[4] && s.q[4]!=provider)return false;s.q[4]=provider;
   node[IntelModule]=intel;node[PageShift]=shift;
   s.q[0]=MapClass;if(node[Class]!=(intel?intel+0xccf08:provider+0x7ffe8))return false;
   s.q[0]=ReadFailed;
   if(!r.get(address,0x10,node[Flags],4) || !r.get(address,0x18,node[Memory]) ||
      !r.get(address,0x28,node[Previous]) || !r.get(address,0x30,node[Next]) ||
      !r.get(address,0x90,node[OwnerTask]) || !r.get(address,0x88,node[Accelerator]) ||
      !r.get(address,0x98,node[Base]) || !r.get(address,0xc8,node[AssignedLength]) ||
      !r.get(address,0xb0,node[ResourceCount]) || !r.get(address,0xc,node[SelfPrepareCount],4) ||
      !r.get(address,0x20,node[LRUSeed],4) || !r.get(node[Memory],0,node[MemClass]))return false;
   s.q[0]=ListShape;if(node[Previous]!=previous)return false;
   s.q[0]=TaskMismatch;if(node[OwnerTask]!=task)return false;
   s.q[0]=MemoryClass;if(node[MemClass]!=(intel?intel+0xcd6e8:provider+0x80570))return false;
   s.q[0]=ReadFailed;if(!r.get(node[Memory],0x40,node[MemoryLength]))return false;
   node[NativeMapLength]=node[MemoryLength];
   if(intel){uint64_t flags=0;s.q[0]=ReadFailed;if(!r.get(address,0x118,flags,4))return false;
    node[ExtraPages]=(flags>>17)&255;const auto extra=node[ExtraPages]<<shift;
    s.q[0]=MapBounds;if(node[NativeMapLength]>UINT64_MAX-extra){memcpy(s.node,node,sizeof(node));return false;}node[NativeMapLength]+=extra;}
   s.q[5]=hash(node,sizeof(node),s.q[5]);
   // Retain the rejected candidate's actual fields, even before containment.
   // This cannot turn a rejected sample into an authorized resource read.
   if(!s.q[3])memcpy(s.node,node,sizeof(node));
   s.q[0]=IndirectAddress;if(node[Flags]&0x40){memcpy(s.node,node,sizeof(node));return false;}
   s.q[0]=MapBounds;
   if(!node[Base] || node[Base]>>47 || !node[MemoryLength] || (!observeOnly && !node[AssignedLength]) ||
      node[NativeMapLength]>(1ULL<<47)-node[Base] || node[AssignedLength]>(1ULL<<47)-node[Base]){memcpy(s.node,node,sizeof(node));return false;}
   if(gpu>=node[Base] && gpu-node[Base]<node[NativeMapLength]) {
    if(!s.q[3])memcpy(s.node,node,sizeof(node));++s.q[3];
    s.q[0]=Ambiguous;if(s.q[3]>1)return false;
    s.q[0]=RangeOverrun;
    if(bytes>node[NativeMapLength]-(gpu-node[Base]) || gpu-node[Base]>=node[AssignedLength] ||
       bytes>node[AssignedLength]-(gpu-node[Base]))return false;
    s.q[0]=SelfNotPrepared;if(!node[SelfPrepareCount])return false;
    s.q[0]=ResourceReferencesUnsupported;if(node[ResourceCount])return false;
   }
   previous=address;address=node[Next];
  }
  s.q[0]=ListShape;if(address || previous!=s.lists[i][1])return false;
 }
 // Recheck list headers within this sample; this cannot exclude ABA or races.
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j) {
  uint64_t value=0;s.q[0]=ReadFailed;
  if(!r.get(task,(i?0x1e8:0x200)+8*j,value,j==2?4:8))return false;
  s.q[0]=Changed;if(value!=s.lists[i][j])return false;
 }
 s.q[0]=NotContained;if(s.q[3]!=1)return false;
 s.q[0]=DeclaredSpan;return true;
}
} // namespace Detail
// Keep edit admission on the strict policy. Observation never produces Receipt,
// never returns true and has a distinct success tag, outside the Reason range.
template<class Read> bool sample(Snapshot &s,uint64_t task,uint64_t gpu,uint64_t bytes,Read read) {
 return Detail::sample(s,task,gpu,bytes,read,false);
}
struct Observation {Snapshot metadata;};
constexpr uint64_t ObservedMetadata=0x100;
template<class Read> void observe(Observation &out,uint64_t task,uint64_t gpu,uint64_t bytes,Read read) {
 // An unrelated node with zero reservation cannot describe the requested
 // address if its independently checked native span excludes it. Its fields
 // remain in the digest. Target reservations, ownership, preparation and
 // associated resources still reject. This is only a failure-time metadata survey.
 if(Detail::sample(out.metadata,task,gpu,bytes,read,true))out.metadata.q[0]=ObservedMetadata;
}
template<class Read> bool begin(Receipt &r,uint64_t task,uint64_t gpu,uint64_t bytes,Read read) {
 memset(&r,0,sizeof(r));r.q[Schema]=2;r.q[Status]=r.q[AfterStatus]=NotChecked;
 r.q[Task]=task;r.q[GPU]=gpu;r.q[Bytes]=bytes;
 const bool ok=sample(r.samples[0],task,gpu,bytes,read);r.q[BeforeStatus]=r.samples[0].q[0];return ok;
}
template<class Read> bool finish(Receipt &r,Read read) {
 ++r.q[Finished];r.q[Status]=NotChecked;
 if(r.q[Schema]!=2 || r.q[BeforeStatus]!=DeclaredSpan){r.q[Status]=r.q[BeforeStatus];return false;}
 const bool ok=sample(r.samples[1],r.q[Task],r.q[GPU],r.q[Bytes],read);
 r.q[AfterStatus]=r.samples[1].q[0];r.q[Status]=r.q[AfterStatus];if(!ok)return false;
 r.q[Status]=Changed;
 // Read counters are diagnostic, not part of metadata identity.
 if(memcmp(r.samples[0].lists,r.samples[1].lists,sizeof(r.samples[0].lists)) ||
    memcmp(r.samples[0].node,r.samples[1].node,sizeof(r.samples[0].node)) ||
    r.samples[0].q[2]!=r.samples[1].q[2] || r.samples[0].q[3]!=r.samples[1].q[3] ||
    r.samples[0].q[4]!=r.samples[1].q[4] || r.samples[0].q[5]!=r.samples[1].q[5])return false;
 r.q[Status]=DeclaredSpan;return true;
}
}
#endif
