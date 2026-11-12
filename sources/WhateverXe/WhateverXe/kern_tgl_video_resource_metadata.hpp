// Bounded CPU metadata. Never an allocation lease or command-edit receipt.
#ifndef KERN_TGL_VIDEO_RESOURCE_METADATA_HPP
#define KERN_TGL_VIDEO_RESOURCE_METADATA_HPP
#include "kern_tgl_video_map.hpp"
namespace TGLVideoResourceMetadata {
constexpr unsigned MaxMaps=32,MaxResources=8;
constexpr uint64_t Complete=0x100;
struct Node {
 uint64_t fields[TGLVideoMap::NodeFields]{};
 uint64_t q[4]{}; // associated array, capacity, aggregate prepare count, reserved
 uint64_t resources[MaxResources][4]{}; // pointer, raw vptr, prepare count, reserved
};
struct Snapshot {
 uint64_t q[8]{}; // status, reads, visited, provider, digest, resource entries, reserved*2
 uint64_t lists[2][3]{};
 Node nodes[MaxMaps]{};
};
struct Capture {
 uint64_t q[8]{}; // schema, outer guard, context ID, task, root, map cap, ref cap, samples
 Snapshot samples[2]{};
};
static_assert(sizeof(Node)==440 && sizeof(Snapshot)==14192 && sizeof(Capture)==28448,"resource metadata schema 1");

template<class Read> void sample(Snapshot &s,uint64_t task,Read read) {
 using namespace TGLVideoMap;
 memset(&s,0,sizeof(s));s.q[0]=TGLVideoMap::Bounds;s.q[4]=14695981039346656037ULL;
 if(!kernelRange(task,0x218))return;
 Reader<Read> r{read,s.q[1]};s.q[0]=ReadFailed;
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j)
  if(!r.get(task,(i?0x1e8:0x200)+8*j,s.lists[i][j],j==2?4:8))return;
 s.q[0]=ListLimit;
 if(s.lists[0][2]>MaxMaps || s.lists[1][2]>MaxMaps-s.lists[0][2])return;
 for(unsigned i=0;i<2;++i) {
  uint64_t address=s.lists[i][0],previous=0;
  for(unsigned j=0;j<s.lists[i][2];++j) {
   s.q[0]=ListShape;if(!kernelRange(address,0x120))return;
   s.q[0]=DuplicateMap;
   for(unsigned k=0;k<s.q[2];++k)if(s.nodes[k].fields[Address]==address)return;
   auto &out=s.nodes[s.q[2]++];auto node=out.fields;node[Address]=address;
   s.q[0]=ReadFailed;if(!r.get(address,0,node[Class]))return;
   uint64_t provider=0,intel=0,shift=0;s.q[0]=DonorMismatch;
   const bool qualified=(node[Class]&4095)==0xf08 ? intelDonor(node[Class],provider,intel,shift,r) : donor(node[Class],provider,r);
   if(!qualified){if(r.failed)s.q[0]=ReadFailed;return;}
   if(s.q[3] && s.q[3]!=provider)return;s.q[3]=provider;
   node[IntelModule]=intel;node[PageShift]=shift;
   // Pin the native aggregate getter before interpreting associated entries.
   const uint8_t code[]={0x8b,0x47,0x0c,0x48,0x8b,0x8f,0xb0,0,0,0,0x48,0x85,0xc9,0x74,0x21,0x55,
    0x48,0x89,0xe5,0x48,0x8b,0x97,0xa0,0,0,0,0x31,0xf6,0x48,0x8b,0x3c,0xf2,
    0x83,0x7f,0x28,1,0x83,0xd8,0xff,0x48,0xff,0xc6,0x48,0x39,0xf1,0x75,0xed,0x5d,0xc3};
   uint8_t actual[sizeof(code)]{};s.q[0]=ReadFailed;
   if(!r(provider+0x52a20,actual,32) || !r(provider+0x52a40,actual+32,sizeof(code)-32))return;
   s.q[0]=DonorMismatch;if(memcmp(code,actual,sizeof(code)))return;
   s.q[0]=ReadFailed;
   if(!r.get(address,0x10,node[Flags],4) || !r.get(address,0x18,node[Memory]) ||
      !r.get(address,0x28,node[Previous]) || !r.get(address,0x30,node[Next]) ||
      !r.get(address,0x90,node[OwnerTask]) || !r.get(address,0x88,node[Accelerator]) ||
      !r.get(address,0x98,node[Base]) || !r.get(address,0xc8,node[AssignedLength]) ||
      !r.get(address,0xb0,node[ResourceCount]) || !r.get(address,0xc,node[SelfPrepareCount],4) ||
      !r.get(address,0x20,node[LRUSeed],4) || !r.get(node[Memory],0,node[MemClass]) ||
      !r.get(address,0xa0,out.q[0]) || !r.get(address,0xa8,out.q[1]))return;
   s.q[0]=ListShape;if(node[Previous]!=previous)return;
   s.q[0]=TaskMismatch;if(node[OwnerTask]!=task)return;
   s.q[0]=MemoryClass;if(node[MemClass]!=(intel?intel+0xcd6e8:provider+0x80570))return;
   s.q[0]=ReadFailed;if(!r.get(node[Memory],0x40,node[MemoryLength]))return;
   node[NativeMapLength]=node[MemoryLength];
   if(intel) {
    uint64_t flags=0;if(!r.get(address,0x118,flags,4))return;
    node[ExtraPages]=(flags>>17)&255;const auto extra=node[ExtraPages]<<shift;
    s.q[0]=MapBounds;if(node[NativeMapLength]>UINT64_MAX-extra)return;
    node[NativeMapLength]+=extra;
   }
   // Reservation=0, self count=0 and associated entries are recorded as native
   // metadata, not rejected as invalid allocations or accepted for access.
   s.q[0]=IndirectAddress;if(node[Flags]&0x40)return;
   s.q[0]=MapBounds;
   if(node[Base]>>47 || node[NativeMapLength]>(1ULL<<47)-node[Base] || node[AssignedLength]>(1ULL<<47)-node[Base])return;
   s.q[0]=ResourceReferencesUnsupported;
   if(node[ResourceCount]>MaxResources || node[ResourceCount]>out.q[1] ||
      (node[ResourceCount] && !kernelRange(out.q[0],8*node[ResourceCount])))return;
   uint32_t prepared=uint32_t(node[SelfPrepareCount]);
   for(unsigned k=0;k<node[ResourceCount];++k) {
    auto &entry=out.resources[k];s.q[0]=ReadFailed;
    if(!r.get(out.q[0],8*k,entry[0]) || !r.get(entry[0],0,entry[1]) || !r.get(entry[0],0x28,entry[2],4))return;
    s.q[0]=TGLVideoMap::Bounds;if(!kernelRange(entry[1],8))return;
    prepared+=entry[2]!=0;++s.q[5];
   }
   out.q[2]=prepared;
   s.q[4]=hash(&out,sizeof(out),s.q[4]);previous=address;address=node[Next];
  }
  s.q[0]=ListShape;if(address || previous!=s.lists[i][1])return;
 }
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j) {
  uint64_t value=0;s.q[0]=ReadFailed;
  if(!r.get(task,(i?0x1e8:0x200)+8*j,value,j==2?4:8))return;
  s.q[0]=Changed;if(value!=s.lists[i][j])return;
 }
 s.q[0]=Complete;
}
} // namespace TGLVideoResourceMetadata
#endif
