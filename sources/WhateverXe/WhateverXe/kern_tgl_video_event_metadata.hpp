// Opt-in CPU event metadata. Never invokes event tests or reads GPU stamp memory.
#ifndef KERN_TGL_VIDEO_EVENT_METADATA_HPP
#define KERN_TGL_VIDEO_EVENT_METADATA_HPP
#include "kern_tgl_video_resource_metadata.hpp"
namespace TGLVideoEventMetadata {
constexpr unsigned MaxMaps=TGLVideoResourceMetadata::MaxMaps,MaxResources=TGLVideoResourceMetadata::MaxResources;
enum Reason {NotChecked,Complete,SourceUnsupported,ReadFailed,DonorMismatch,Changed,ChannelUnsupported};
enum Header {Schema,Guard,ContextID,Task,Root,MapCap,ResourceCap,Samples,Ring,Context,Stamp,CommandHash,BeginNS,EndNS,Phase,Reserved,HeaderFields};
enum Field {Status,Reads,Visited,Provider,Intel,Accelerator,Machine,MachineClass,NumStamps,OverrideBefore,OverrideAfter,SourceDigest,SourceMaps,Digest,Reserved0,Reserved1,Fields};
struct Node {
 uint64_t q[8]{}; // map, class, flags, GPU base, native span, resource count, self prepare, LRU
 uint64_t event[8]{}; // native packed {required stamp:32, channel:32}; 0xffffffff channel is empty
 uint64_t cached[8][2]{}; // CPU cached completed/submitted, only for nonempty valid channels
 uint64_t resources[MaxResources][8]{}; // pointer, class, flags16, subtype8, indices8*2, channel count32, event pointer
};
struct Snapshot {uint64_t q[Fields]{};Node nodes[MaxMaps]{};};
struct Capture {uint64_t q[HeaderFields]{};Snapshot samples[2]{};};
static_assert(sizeof(Node)==768 && sizeof(Snapshot)==24704 && sizeof(Capture)==49536,"event metadata schema 1");
inline void init(Capture &c,uint64_t contextID,uint64_t task,uint64_t root,uint64_t ring,uint64_t context,uint64_t stamp,uint64_t hash,uint64_t phase) {
 memset(&c,0,sizeof(c));c.q[Schema]=1;c.q[Guard]=1;c.q[ContextID]=contextID;c.q[Task]=task;c.q[Root]=root;
 c.q[MapCap]=MaxMaps;c.q[ResourceCap]=MaxResources;c.q[Samples]=2;c.q[Ring]=ring;c.q[Context]=context;
 c.q[Stamp]=stamp;c.q[CommandHash]=hash;c.q[Phase]=phase;
}
template<class Read> bool qualify(uint64_t provider,uint64_t intel,uint64_t machine,uint64_t vptr,TGLVideoMap::Reader<Read> &r) {
 if(!TGLVideoMap::kernelRange(provider,0x806a0) || !TGLVideoMap::kernelRange(intel,0xd9400) ||
    !TGLVideoMap::kernelRange(machine,0x40) || vptr!=intel+0xcea08)return false;
 const unsigned slots[]={0x148,0x180,0x190,0x1d0};const uint64_t targets[]={0x307f6,0x30414,0x309a2,0x3123c};
 for(unsigned i=0;i<4;++i){uint64_t value=0;if(!r.get(vptr,slots[i],value) || value!=provider+targets[i])return false;}
 const uint8_t testEvent[]={0x55,0x48,0x89,0xe5,0x31,0xc0,0x48,0x8b,0x14,0xc6,0x83,0xfa,0xff,0x74,0x4b,0x48,0x89,0xd1,0x48,0xc1,0xe9,0x20,0x48,0x63,0xd2,0x4c,0x8d,0x04,0xd5,0x00,0x00,0x00,0x00,0x4f,0x8d,0x04,0x40,0x41,0x89,0xc9,0x46,0x2b,0x8c,0x07,0xf8,0x00,0x00,0x00,0x45,0x85,0xc9,0x7e,0x25,0x4c,0x8b,0x4f,0x28,0x49,0x8b,0x14,0xd1,0x8b,0x12,0x42,0x89,0x94,0x07,0xf8,0x00,0x00,0x00,0x29,0xd1,0x85,0xc9,0x7e,0x0d,0x48,0x8b,0x4f,0x10,0x83,0xb9,0xc8,0x0d,0x00,0x00,0x00,0x74,0x0d,0x48,0xff,0xc0,0x48,0x83,0xf8,0x08,0x75,0xa3,0xb0,0x01,0x5d,0xc3,0x31,0xc0,0xeb,0xfa};
 const uint8_t getNumStamps[]={0x55,0x48,0x89,0xe5,0x8b,0x47,0x30,0x5d,0xc3};
 const uint8_t mapTestEvent[]={0x55,0x48,0x89,0xe5,0x48,0x8b,0x87,0x88,0x00,0x00,0x00,0x48,0x8b,0x80,0x80,0x03,0x00,0x00,0x48,0x8d,0x77,0x38,0x48,0x8b,0x08,0x48,0x8b,0x89,0x90,0x01,0x00,0x00,0x48,0x89,0xc7,0x5d,0xff,0xe1};
 const uint8_t setEventStamp[]={0x48,0x63,0xce,0x48,0x8d,0x0c,0x49,0x8b,0x8c,0xcf,0xfc,0x00,0x00,0x00,0x48,0xc1,0xe1,0x20,0x89,0xf6,0x48,0x09,0xce,0x48,0x89,0x34,0xc2,0x5d,0xc3};
 const uint8_t *expected[]={testEvent,getNumStamps,mapTestEvent,setEventStamp};
 const unsigned offsets[]={0x30414,0x1294a,0x52462,0x312bc};
 const unsigned sizes[]={sizeof(testEvent),sizeof(getNumStamps),sizeof(mapTestEvent),sizeof(setEventStamp)};
 for(unsigned i=0;i<4;++i)for(unsigned at=0;at<sizes[i];) {
  uint8_t bytes[32]{};const unsigned n=sizes[i]-at<32?sizes[i]-at:32;
  if(!r(provider+offsets[i]+at,bytes,n) || memcmp(bytes,expected[i]+at,n))return false;at+=n;
 }
 return true;
}
// Recheck the recorded map and array identity. Equal observations cannot exclude ABA.
template<class Read> bool mapIdentity(const TGLVideoResourceMetadata::Node &source,TGLVideoMap::Reader<Read> &r) {
 using namespace TGLVideoMap;const unsigned fields[]={Class,Flags,Memory,Previous,Next,OwnerTask,TGLVideoMap::Accelerator,Base,AssignedLength,ResourceCount,SelfPrepareCount,LRUSeed};
 const unsigned offsets[]={0,0x10,0x18,0x28,0x30,0x90,0x88,0x98,0xc8,0xb0,0xc,0x20};
 for(unsigned i=0;i<12;++i){uint64_t value=0;const unsigned n=(i==1 || i==10 || i==11)?4:8;
  if(!r.get(source.fields[Address],offsets[i],value,n) || value!=source.fields[fields[i]])return false;}
 uint64_t array=0,capacity=0;
 return r.get(source.fields[Address],0xa0,array) && r.get(source.fields[Address],0xa8,capacity) && array==source.q[0] && capacity==source.q[1];
}
template<class Read> bool resource(uint64_t (&out)[8],const uint64_t (&source)[4],uint64_t array,unsigned index,uint64_t intel,TGLVideoMap::Reader<Read> &r) {
 uint64_t pointer=0,count=0;
 if(!r.get(array,8*index,pointer) || pointer!=source[0] || !TGLVideoMap::kernelRange(pointer,0xa4))return false;
 out[0]=pointer;
 return r.get(pointer,0,out[1]) && out[1]==source[1] && out[1]==intel+0xd93e0 &&
  r.get(pointer,0x28,count,4) && count==source[2] && r.get(pointer,0xe,out[2],2) &&
  r.get(pointer,0x14,out[3],1) && r.get(pointer,0x15,out[4],1) && r.get(pointer,0x16,out[5],1) &&
  r.get(pointer,0xa0,out[6],4) && r.get(pointer,0x38,out[7]);
}
template<class Read> void sample(Snapshot &s,const TGLVideoResourceMetadata::Snapshot &source,uint64_t task,Read read) {
 using namespace TGLVideoMap;
 memset(&s,0,sizeof(s));s.q[Status]=SourceUnsupported;s.q[SourceDigest]=source.q[4];s.q[SourceMaps]=source.q[2];
 s.q[Digest]=14695981039346656037ULL;
 if(source.q[0]!=TGLVideoResourceMetadata::Complete || !source.q[2] || source.q[2]>MaxMaps || !kernelRange(task,0x218))return;
 Reader<Read> r{read,s.q[Reads]};
 const auto &first=source.nodes[0];auto &q=s.q;
 q[Provider]=source.q[3];q[Intel]=first.fields[IntelModule];q[Accelerator]=first.fields[TGLVideoMap::Accelerator];
 if(!q[Intel] || !kernelRange(q[Accelerator],0xdcc))return;
 uint64_t provider=0,intel=0,shift=0;
 q[Status]=DonorMismatch;
 if(!intelDonor(first.fields[Class],provider,intel,shift,r) || provider!=q[Provider] || intel!=q[Intel]){if(r.failed)q[Status]=ReadFailed;return;}
 q[Status]=ReadFailed;
 if(!r.get(q[Accelerator],0x380,q[Machine]) || !r.get(q[Machine],0,q[MachineClass]))return;
 q[Status]=DonorMismatch;
 if(!qualify(q[Provider],q[Intel],q[Machine],q[MachineClass],r)){if(r.failed)q[Status]=ReadFailed;return;}
 uint64_t back=0;q[Status]=ReadFailed;
 if(!r.get(q[Machine],0x10,back) || !r.get(q[Machine],0x30,q[NumStamps],4) || !r.get(q[Accelerator],0xdc8,q[OverrideBefore],4))return;
 q[Status]=DonorMismatch;if(back!=q[Accelerator] || !q[NumStamps] || q[NumStamps]>128)return;
 for(unsigned i=0;i<source.q[2];++i) {
  const auto &b=source.nodes[i];const auto f=b.fields;auto &out=s.nodes[i];
  q[Status]=SourceUnsupported;
  if(f[Class]!=q[Intel]+0xccf08 || f[IntelModule]!=q[Intel] || f[OwnerTask]!=task ||
     f[TGLVideoMap::Accelerator]!=q[Accelerator] || f[ResourceCount]>MaxResources ||
     f[ResourceCount]>b.q[1] || (f[ResourceCount] && !kernelRange(b.q[0],8*f[ResourceCount])))return;
  for(unsigned j=0;j<i;++j)if(source.nodes[j].fields[Address]==f[Address])return;
  ++q[Visited];q[Status]=Changed;if(!mapIdentity(b,r)){if(r.failed)q[Status]=ReadFailed;return;}
  const unsigned select[]={Address,Class,Flags,Base,NativeMapLength,ResourceCount,SelfPrepareCount,LRUSeed};
  for(unsigned j=0;j<8;++j)out.q[j]=f[select[j]];
  q[Status]=ReadFailed;
  if(!r(f[Address]+0x38,out.event,32) || !r(f[Address]+0x58,out.event+4,32))return;
  for(unsigned j=0;j<8;++j) {
   const uint32_t channel=uint32_t(out.event[j]);if(channel==UINT32_MAX)continue;
   q[Status]=ChannelUnsupported;if(channel>=q[NumStamps])return;
   q[Status]=ReadFailed;
   if(!r.get(q[Machine],0xf8+24*channel,out.cached[j][0],4) || !r.get(q[Machine],0xfc+24*channel,out.cached[j][1],4))return;
  }
  for(unsigned j=0;j<f[ResourceCount];++j) {
   q[Status]=Changed;if(!resource(out.resources[j],b.resources[j],b.q[0],j,q[Intel],r)){if(r.failed)q[Status]=ReadFailed;return;}
  }
  // Sequential rechecks only: no locks, waits, event method calls, writes,
  // GPU/shared stamp-pointer dereferences or payload reads.
  uint64_t after[8]{};q[Status]=ReadFailed;
  if(!r(f[Address]+0x38,after,32) || !r(f[Address]+0x58,after+4,32))return;
  q[Status]=Changed;if(memcmp(after,out.event,sizeof(after)))return;
  for(unsigned j=0;j<8;++j) {
   const uint32_t channel=uint32_t(out.event[j]);if(channel==UINT32_MAX)continue;
   uint64_t cached=0,submitted=0;q[Status]=ReadFailed;
   if(!r.get(q[Machine],0xf8+24*channel,cached,4) || !r.get(q[Machine],0xfc+24*channel,submitted,4))return;
   q[Status]=Changed;if(cached!=out.cached[j][0] || submitted!=out.cached[j][1])return;
  }
  for(unsigned j=0;j<f[ResourceCount];++j) {
   uint64_t fields[8]{};q[Status]=Changed;
   if(!resource(fields,b.resources[j],b.q[0],j,q[Intel],r)){if(r.failed)q[Status]=ReadFailed;return;}
   if(memcmp(fields,out.resources[j],sizeof(fields)))return;
  }
  q[Status]=Changed;if(!mapIdentity(b,r)){if(r.failed)q[Status]=ReadFailed;return;}
  q[Digest]=hash(&out,sizeof(out),q[Digest]);
 }
 for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j) {
  uint64_t value=0;q[Status]=ReadFailed;if(!r.get(task,(i?0x1e8:0x200)+8*j,value,j==2?4:8))return;
  q[Status]=Changed;if(value!=source.lists[i][j])return;
 }
 uint64_t machine=0,vptr=0,count=0;q[Status]=ReadFailed;
 if(!r.get(q[Accelerator],0x380,machine) || !r.get(q[Machine],0,vptr) ||
    !r.get(q[Machine],0x30,count,4) || !r.get(q[Accelerator],0xdc8,q[OverrideAfter],4))return;
 q[Status]=Changed;
 if(machine!=q[Machine] || vptr!=q[MachineClass] || count!=q[NumStamps] || q[OverrideBefore]!=q[OverrideAfter])return;
 q[Status]=Complete;
}
}
#endif
