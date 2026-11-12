// Bounded CPU-visible submission address checks, not a mapping lease, GPU TLB
// observation, cache flush, allocation extent/generation or completion proof.
#ifndef KERN_TGL_VIDEO_ALIAS_HPP
#define KERN_TGL_VIDEO_ALIAS_HPP
#include <stdint.h>
#include <string.h>
#include "kern_tgl_ppgtt.hpp"
#include "kern_tgl_vm.hpp"
namespace TGLVideoAlias {
constexpr unsigned MaxBytes=4096, MaxPages=2;
enum Reason {Ready,NotChecked,Bounds,OwnerRead,OwnerMismatch,ClassMismatch,
 LRCBounds,CPURoot,GPUMapping,Chain,RootMismatch,CPUTranslation,AliasMismatch,
 MappingChanged,OwnerChanged,RootChanged};
enum Field {Schema,Status,Ring,Engine,ExpectedClass,CPU,GPU,Bytes,Pages,
 CPURootBefore,CPURootAfter,BeforeQualified,AtApplyStatus,AtApplyAttempted,
 Finished,Reserved,Fields};
enum OwnerField {Context,Owner,Task,Table,TableClass,LRCBuffer,LRCCPU,LRCSize,
 Root,ContextID,OwnerFields};
struct Page {
 uint64_t q[8]{}; // GPU address, CPU address, covered bytes, status; reserved
 TGLPPGTT::Mapping gpu[2]{};
 TGLPPGTT::Chain chain[2]{};
 TGLVM::Mapping cpu[2]{};
};
struct Receipt {
 uint64_t q[Fields]{};
 uint64_t owner[2][OwnerFields]{};
 Page pages[MaxPages]{};
};
static_assert(sizeof(Receipt)==2176,"versioned address receipt layout");
inline bool kernelRange(uint64_t p,uint64_t n) {
 return n && (p>>47)==0x1ffff && p<=UINT64_MAX-(n-1) && ((p+n-1)>>47)==0x1ffff;
}
template<class Read>
bool ownership(uint64_t *q,uint64_t ring,uint64_t engine,uint64_t expectedClass,Read &read,Reason &reason) {
 auto get=[&](uint64_t p,unsigned offset,uint64_t &v,unsigned n=8) {
  v=0;return kernelRange(p,uint64_t(offset)+n) && read(p+offset,&v,n)==n;
 };
 uint64_t back=0,owner=0,actualEngine=0,lrc=0,hi=0,lo=0;
 reason=OwnerRead;
 if(!get(ring,0x20,q[Context]) || !get(ring,0x10,q[Owner]) ||
    !get(q[Context],0x50,owner) || !get(q[Context],0xb0,back) ||
    !get(ring,0x40,actualEngine,1))return false;
 reason=OwnerMismatch;
 if(!q[Owner] || owner!=q[Owner] || back!=ring || actualEngine!=engine)return false;
 reason=OwnerRead;
 if(!get(q[Context],0x58,q[Task]) || !get(q[Task],0x260,q[Table]) ||
    !get(q[Table],0,q[TableClass]))return false;
 reason=ClassMismatch;
 if(!expectedClass || q[TableClass]!=expectedClass)return false;
 reason=OwnerRead;
 if(!get(q[Context],0x98,q[LRCBuffer]) || !get(q[LRCBuffer],0x38,q[LRCCPU]) ||
    !get(q[LRCBuffer],0x20,q[LRCSize]) || !get(ring,0x18,lrc) ||
    !get(q[Context],0x7c,q[ContextID],4))return false;
 reason=LRCBounds;
 if(lrc!=q[LRCCPU] || q[LRCSize]<8192 || q[LRCSize]>81920 ||
    (q[LRCSize]&4095) || (q[LRCCPU]&4095) || !kernelRange(q[LRCCPU],q[LRCSize]))return false;
 reason=OwnerRead;
 if(!get(q[LRCCPU],0x10c4,hi,4) || !get(q[LRCCPU],0x10cc,lo,4))return false;
 q[Root]=(hi<<32)|lo;reason=RootMismatch;
 if(!q[Root] || (q[Root]&4095))return false;
 reason=Ready;return true;
}
template<class Read,class CPUWalk>
bool sample(Page &p,unsigned phase,const uint64_t *owner,uint64_t cpuRoot,Read &read,CPUWalk &cpuWalk) {
 auto get=[&](uint64_t address,uint64_t &v) {return kernelRange(address,8) && read(address,&v,8)==8;};
 auto physical=[&](uint64_t address,uint64_t &v) {const auto m=cpuWalk(cpuRoot,address);v=m.physical;return m.status==0;};
 p.gpu[phase]=TGLPPGTT::inspect(owner[Table],p.q[0],get);
 p.q[3]=GPUMapping;if(p.gpu[phase].status)return false;
 p.q[3]=RootMismatch;if(p.gpu[phase].rootPhysical!=owner[Root])return false;
 p.chain[phase]=TGLPPGTT::inspectChain(owner[Table],p.q[0],get,physical);
 p.q[3]=Chain;if(p.chain[phase].q[1] || p.chain[phase].q[6] || p.chain[phase].q[4]!=owner[Root] ||
    p.chain[phase].q[5]!=p.gpu[phase].physical || p.chain[phase].rows[3][5]!=p.gpu[phase].pte)return false;
 for(unsigned i=0;i<4;++i)if(p.chain[phase].rows[i][0]!=p.gpu[phase].descriptors[i])return false;
 p.cpu[phase]=cpuWalk(cpuRoot,p.q[1]);
 p.q[3]=CPUTranslation;if(p.cpu[phase].status)return false;
 p.q[3]=AliasMismatch;if(p.cpu[phase].physical!=p.gpu[phase].physical)return false;
 p.q[3]=Ready;return true;
}
template<class Read,class CPUWalk>
bool begin(Receipt &r,uint64_t ring,uint64_t engine,uint64_t expectedClass,
 uint64_t cpu,uint64_t gpu,uint64_t bytes,uint64_t kernelPmap,Read read,CPUWalk cpuWalk) {
 memset(&r,0,sizeof(r));auto q=r.q;q[Schema]=1;q[Status]=Bounds;q[AtApplyStatus]=NotChecked;
 q[Ring]=ring;q[Engine]=engine;q[ExpectedClass]=expectedClass;q[CPU]=cpu;q[GPU]=gpu;q[Bytes]=bytes;
 if((engine!=0 && engine!=3) || !kernelRange(ring,0x41) || !kernelRange(cpu,bytes) ||
    !bytes || bytes>MaxBytes || ((cpu|gpu|bytes)&3) || !gpu || gpu>>47 || bytes>(1ULL<<47)-gpu ||
    (cpu&4095)!=(gpu&4095) || !kernelRange(kernelPmap,0x58))return false;
 Reason reason=NotChecked;
 if(!ownership(r.owner[0],ring,engine,expectedClass,read,reason)){q[Status]=reason;return false;}
 q[Status]=CPURoot;
 if(read(kernelPmap+0x50,q+CPURootBefore,8)!=8 || !kernelRange(q[CPURootBefore],4096) || (q[CPURootBefore]&4095))return false;
 for(uint64_t at=0;at<bytes;) {
  const unsigned n=static_cast<unsigned>(q[Pages]);if(n==MaxPages){q[Status]=Bounds;return false;}
  auto &p=r.pages[n];p.q[0]=gpu+at;p.q[1]=cpu+at;
  p.q[2]=bytes-at<4096-((gpu+at)&4095)?bytes-at:4096-((gpu+at)&4095);++q[Pages];
  if(!sample(p,0,r.owner[0],q[CPURootBefore],read,cpuWalk)){q[Status]=p.q[3];return false;}
  at+=p.q[2];
 }
 q[BeforeQualified]=1;q[Status]=NotChecked;return true;
}
template<class Read,class CPUWalk>
bool finish(Receipt &r,uint64_t kernelPmap,Read read,CPUWalk cpuWalk) {
 auto q=r.q;++q[Finished];
 if(q[Schema]!=1 || !q[BeforeQualified] || !q[Pages] || q[Pages]>MaxPages)return false;
 Reason reason=NotChecked;
 if(!ownership(r.owner[1],q[Ring],q[Engine],q[ExpectedClass],read,reason)){q[Status]=reason;return false;}
 q[Status]=OwnerChanged;if(memcmp(r.owner[0],r.owner[1],sizeof(r.owner[0])))return false;
 q[Status]=CPURoot;
 if(!kernelRange(kernelPmap,0x58) || read(kernelPmap+0x50,q+CPURootAfter,8)!=8)return false;
 q[Status]=RootChanged;if(q[CPURootBefore]!=q[CPURootAfter])return false;
 for(unsigned n=0;n<q[Pages];++n) {
  auto &p=r.pages[n];
  if(!sample(p,1,r.owner[1],q[CPURootAfter],read,cpuWalk)){q[Status]=p.q[3];return false;}
  q[Status]=MappingChanged;
  if(memcmp(&p.gpu[0],&p.gpu[1],sizeof(p.gpu[0])) ||
     memcmp(&p.chain[0],&p.chain[1],sizeof(p.chain[0])) ||
     memcmp(&p.cpu[0],&p.cpu[1],sizeof(p.cpu[0])))return false;
 }
 q[Status]=Ready;return true;
}
}
#endif
