// Opt-in CPU metadata before native submission, never a mapping lease or fence.
#ifndef KERN_TGL_VIDEO_SUBMIT_METADATA_HPP
#define KERN_TGL_VIDEO_SUBMIT_METADATA_HPP
#include "kern_tgl_video_audit.hpp"
#include "kern_tgl_video_resource_metadata.hpp"
#include "kern_tgl_video_event_metadata.hpp"
namespace TGLVideoSubmitMetadata {
enum Field {Schema,Guard,BeginNS,EndNS,AuditBytes,MetadataBytes,Reserved0,Reserved1,Fields};
enum Reason {Observed,NotChecked,OwnerChanged,CommandChanged,CommandReadFailed};
struct Capture {
 uint64_t q[Fields]{};
 TGLVideoAudit::Record audit{};
 TGLVideoResourceMetadata::Capture metadata{};
};
static_assert(sizeof(Capture)==35968,"submission metadata schema 1");
// Select the first complete, CPU-alias-qualified VDEnc command receipt. This
// does not select by a global last-context value or accept truncated prefixes.
inline bool eligible(const TGLVideoAudit::Record &r) {
 using namespace TGLVideoAudit;
 const auto q=r.q;const auto a=r.alias.q;
 if(q[TGLVideoAudit::Schema]!=4 || q[OwnerGuard] || q[Engine]!=3 ||
    !q[Context] || !q[ContextID] || !q[Stamp] || !q[Words] || q[Words]>PrefixBytes/4 ||
    q[CopiedBefore]!=q[Words]*4 || q[CopiedAfter]!=q[Words]*4 ||
    !(q[Category]&2) || q[ParseStop]!=q[Words]*4 ||
    a[TGLVideoAlias::Schema]!=1 || a[TGLVideoAlias::Status]!=TGLVideoAlias::Ready ||
    !a[TGLVideoAlias::BeforeQualified] || !a[TGLVideoAlias::Finished] ||
    a[TGLVideoAlias::Ring]!=q[Ring] || a[TGLVideoAlias::GPU]!=q[TGLVideoAudit::GPU] ||
    a[TGLVideoAlias::Bytes]!=q[Words]*4 || a[TGLVideoAlias::Engine]!=3 ||
    !a[TGLVideoAlias::ExpectedClass])return false;
 const auto owner=r.alias.owner[1];
 return owner[TGLVideoAlias::Context]==q[Context] && owner[TGLVideoAlias::ContextID]==q[ContextID] &&
  owner[TGLVideoAlias::Task] && owner[TGLVideoAlias::Root] &&
  !memcmp(r.alias.owner[0],owner,sizeof(r.alias.owner[0]));
}
template<class Read> bool sameCommand(const TGLVideoAudit::Record &r,Read &read,Reason &reason) {
 uint8_t bytes[32];uint64_t h=14695981039346656037ULL;
 const auto p=r.alias.q[TGLVideoAlias::CPU],n=r.q[TGLVideoAudit::Words]*4;
 reason=CommandReadFailed;
 if(!TGLVideoMap::kernelRange(p,n))return false;
 for(unsigned at=0;at<n;) {
  const unsigned size=n-at<sizeof(bytes)?static_cast<unsigned>(n-at):sizeof(bytes);
  if(read(p+at,bytes,size)!=size)return false;
  h=TGLVideoAudit::hash(bytes,size,h);at+=size;
 }
 reason=CommandChanged;return h==r.q[TGLVideoAudit::HashAfter];
}
template<class Read> bool sameOwner(const TGLVideoAudit::Record &r,Read &read) {
 uint64_t owner[TGLVideoAlias::OwnerFields]{};TGLVideoAlias::Reason reason;
 return TGLVideoAlias::ownership(owner,r.q[TGLVideoAudit::Ring],3,
   r.alias.q[TGLVideoAlias::ExpectedClass],read,reason) &&
   !memcmp(owner,r.alias.owner[1],sizeof(owner));
}
// Call only after eligible(). Preserve failed/changed observations as unknown.
// All pointers are bounded CPU reads; no resource methods, writes or GPU reads.
template<class Read> void collect(Capture &out,const TGLVideoAudit::Record &r,Read read,TGLVideoEventMetadata::Capture *events=nullptr) {
 memset(&out,0,sizeof(out));out.q[Schema]=1;out.q[Guard]=NotChecked;
 out.q[AuditBytes]=sizeof(r);out.q[MetadataBytes]=sizeof(out.metadata);out.audit=r;
 if(events)TGLVideoEventMetadata::init(*events,r.q[TGLVideoAudit::ContextID],
  r.alias.owner[1][TGLVideoAlias::Task],r.alias.owner[1][TGLVideoAlias::Root],r.q[TGLVideoAudit::Ring],
  r.q[TGLVideoAudit::Context],r.q[TGLVideoAudit::Stamp],r.q[TGLVideoAudit::HashAfter],0);
 if(!eligible(r))return;
 auto &m=out.metadata;const auto owner=r.alias.owner[1];
 m.q[0]=1;m.q[1]=1;m.q[2]=r.q[TGLVideoAudit::ContextID];
 m.q[3]=owner[TGLVideoAlias::Task];m.q[4]=owner[TGLVideoAlias::Root];
 m.q[5]=TGLVideoResourceMetadata::MaxMaps;m.q[6]=TGLVideoResourceMetadata::MaxResources;m.q[7]=2;
 Reason reason=CommandReadFailed;
 if(!sameCommand(r,read,reason)){out.q[Guard]=reason;return;}
 out.q[Guard]=OwnerChanged;if(!sameOwner(r,read))return;
 for(unsigned i=0;i<2;++i) {
  TGLVideoResourceMetadata::sample(m.samples[i],m.q[3],read);
  if(events)TGLVideoEventMetadata::sample(events->samples[i],m.samples[i],m.q[3],read);
 }
 if(!sameOwner(r,read))return;
 if(!sameCommand(r,read,reason)){out.q[Guard]=reason;return;}
 // Observed refers only to stable outer CPU metadata. Snapshot statuses,
 // digests and resource counts remain independently checked by the decoder.
 m.q[1]=0;out.q[Guard]=Observed;
 if(events)events->q[TGLVideoEventMetadata::Guard]=0;
}
}
#endif
