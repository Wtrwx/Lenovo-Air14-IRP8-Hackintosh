#include "kern_tgl_video_audit.hpp"
#include <cassert>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdio>
using namespace TGLVideoAudit;
static std::vector<uint32_t> broken(unsigned slices=4) {
 assert(slices==1 || slices==4);
 std::vector<uint32_t> v;
 auto add=[&](uint32_t h,unsigned n){unsigned at=v.size();v.resize(at+n);v[at]=h;return at;};
 auto p=add(0x70000003,5);v[p+1]=0x12;p=add(0x70800000,2);v[p+1]=0x82;
 p=add(0x71000013,21);v[p+1]=108*68;v[p+2]=(67<<16)|107;v[p+4]=6;
 for(unsigned i=0;i<slices;++i){auto s=add(0x71030009,11);unsigned start=i*(68/slices),end=start+68/slices;v[s+4]=(start*2<<24)|(start*2*108);v[s+5]=(i+1==slices?68:end*2)<<16;v[s+6]=i+1==slices?1<<19:0;
  auto w=add(0x70870002,4);v[w+1]=start;v[w+2]=end;}
 return v;
}
static void validRecord(const Record &r){assert(r.q[0]==7 && r.q[1]==~r.q[2]);for(unsigned i=0;i<sizeof(r.before);i++)assert(r.before[i]==uint8_t(r.q[2]));}
int main(){
 assert(hash(nullptr,0)==14695981039346656037ULL);assert(hash((const uint8_t*)"a",1)==0xaf63dc4c8601ec8cULL);
 unsigned stop=0;uint32_t fake[]={0x11000001,0x70000003,0x70800000};assert(!category((uint8_t*)fake,sizeof(fake),stop)&&stop==sizeof(fake));
 uint32_t truncated[]={0x70800000};assert(!category((uint8_t*)truncated,sizeof(truncated),stop)&&stop==0);
 uint32_t media[17]={0x7103000f};assert(category((uint8_t*)media,sizeof(media),stop)==4 && stop==sizeof(media));
 auto v=broken(),original=v;assert(category((uint8_t*)v.data(),v.size()*4,stop)==3&&stop==v.size()*4);
 // Extended framing is observable without relaxing either write planner.
 {uint32_t extended[]={0x70800004,0x82,0x002b030a,0x07000303,0x70800000,0x70800004};
  auto before=std::vector<uint32_t>(extended,extended+6);
  assert(category((uint8_t*)extended,sizeof(extended),stop)==2&&stop==sizeof(extended));
  for(unsigned n=0;n<sizeof(extended);++n)assert(!category((uint8_t*)extended,n,stop)&&stop==0);
  assert(std::vector<uint32_t>(extended,extended+6)==before);
  uint32_t payload[]={0x11000001,0x70800004,0x82};
  assert(!category((uint8_t*)payload,sizeof(payload),stop)&&stop==sizeof(payload));
  auto longMode=original;longMode[5]=0x70800004;
  longMode.insert(longMode.begin()+7,{0x002b030a,0x07000303,0,0});auto saved=longMode;
  assert(category((uint8_t*)longMode.data(),longMode.size()*4,stop)==3&&stop==longMode.size()*4);
  TGLVideoProgressive::Plan no;TGLVideoProgressive::Reason why;
  assert(!TGLVideoProgressive::plan(longMode.data(),longMode.size(),no,&why));
  assert(why==TGLVideoProgressive::Reason::ImageOrder&&no.count==0&&longMode==saved);
 }
 TGLVideoProgressive::Plan p;TGLVideoProgressive::Reason reason;
 assert(TGLVideoProgressive::plan(v.data(),v.size(),p,&reason)&&reason==TGLVideoProgressive::Reason::Ready&&p.count==9&&p.slices==4);
 auto changed=v;changed[p.edits[p.count-1].index]^=1;auto saved=changed;assert(!TGLVideoProgressive::apply(changed.data(),changed.size(),p)&&saved==changed);
 assert(TGLVideoProgressive::apply(v.data(),v.size(),p));assert(!TGLVideoProgressive::plan(v.data(),v.size(),p,&reason)&&reason==TGLVideoProgressive::Reason::ImageState);
 for(unsigned n=0;n<original.size();n++){auto before=original;assert(!TGLVideoProgressive::plan(original.data(),n,p,&reason));assert(before==original);}
 // A real single full-frame slice must retain the image correction, even
 // though its start/end edits are identities. No boundary command is removed.
 auto one=broken(1),oneBefore=one;
 assert(TGLVideoProgressive::plan(one.data(),one.size(),p,&reason)&&p.slices==1&&p.count==3);
 assert(p.edits[0].before==p.edits[0].after && p.edits[1].before==p.edits[1].after);
 auto stale=one;stale[p.edits[0].index]=1;auto staleBefore=stale;
 assert(!TGLVideoProgressive::apply(stale.data(),stale.size(),p)&&stale==staleBefore);
 assert(TGLVideoProgressive::apply(one.data(),one.size(),p));
 unsigned differences=0;for(unsigned i=0;i<one.size();++i)if(one[i]!=oneBefore[i]){++differences;assert(i==11&&oneBefore[i]==6&&one[i]==4);}assert(differences==1);
 assert(!TGLVideoProgressive::plan(one.data(),one.size(),p,&reason)&&reason==TGLVideoProgressive::Reason::ImageState);
 // Single-slice admission still rejects field state, nonzero starts,
 // incomplete coverage, wrong last flags, and malformed packet boundaries.
 auto rejects=[&](unsigned at,uint32_t value,TGLVideoProgressive::Reason expected){auto x=oneBefore;x[at]=value;auto saved=x;assert(!TGLVideoProgressive::plan(x.data(),x.size(),p,&reason)&&reason==expected&&x==saved);};
 rejects(10,0x100,TGLVideoProgressive::Reason::ImageState);
 rejects(11,4,TGLVideoProgressive::Reason::ImageState);
 rejects(32,1,TGLVideoProgressive::Reason::SliceGeometry);
 rejects(33,67U<<16,TGLVideoProgressive::Reason::SliceGeometry);
 rejects(34,0,TGLVideoProgressive::Reason::LastSlice);
 rejects(40,1,TGLVideoProgressive::Reason::WalkerGeometry);
 rejects(41,67,TGLVideoProgressive::Reason::LastSlice);
 rejects(42,1,TGLVideoProgressive::Reason::WalkerState);
 for(unsigned n=0;n<oneBefore.size();++n)assert(!TGLVideoProgressive::plan(oneBefore.data(),n,p,&reason));
 auto h=std::make_unique<History<4,2>>();Record r;
 for(unsigned i=0;i<10;i++){r.q[2]=i;h->append(r);}assert(h->attempts==10);
 for(unsigned i=0;i<2;i++){Record out;assert(h->copy(h->early[i],out)==i+1&&out.q[2]==i);}
 assert(!h->put(h->recent[1],2,r)); // Older receipt cannot replace sequence 10.
 h->recent[0].busy=1;Record out;assert(h->copy(h->recent[0],out)==0);h->recent[0].busy=0;
 h->freeze();auto attempts=h->attempts;h->append(r);assert(h->attempts==attempts);
 auto parallel=std::make_unique<History<8,4>>();std::atomic<unsigned> writers{4};std::vector<std::thread> threads;
 for(unsigned t=0;t<4;t++)threads.emplace_back([&,t]{for(unsigned n=0;n<800;n++){Record x;x.q[0]=7;x.q[2]=n*4+t;x.q[1]=~x.q[2];memset(x.before,uint8_t(x.q[2]),sizeof(x.before));parallel->append(x);}--writers;});
 while(writers.load()){for(auto &slot:parallel->recent){Record x;if(parallel->copy(slot,x))validRecord(x);}}
 for(auto &thread:threads)thread.join();parallel->freeze();for(auto &slot:parallel->recent){Record x;if(parallel->copy(slot,x))validRecord(x);}
 assert(parallel->attempts==3200);
 puts("PASS video audit: prefix/packet boundaries, no payload scan, unchanged geometry gates, all-or-nothing edits, receipt collisions/holes/freeze/concurrent snapshots");
}
