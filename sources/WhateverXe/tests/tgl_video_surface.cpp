#include "kern_tgl_video_surface.hpp"
#include <vector>
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
using namespace TGLVideoSurface;
static std::vector<uint32_t> batch(unsigned slices=4) {
 std::vector<uint32_t> v;
 auto add=[&](uint32_t h,unsigned n){unsigned i=v.size();v.resize(i+n);v[i]=h;return i;};
 auto p=add(0x70000003,5);v[p+1]=0x12;
 for(unsigned s=0;s<3;s++){p=add(0x70010004,6);v[p+1]=s==0?4:s==1?0:5;v[p+2]=s==2?0x043c1af0:0x10fc6bf0;v[p+3]=s==2?0x48000ffb:0x480037fb;v[p+4]=v[p+5]=s==2?288:1088;}
 p=add(0x70800000,2);v[p+1]=0x82;
 for(unsigned s=0;s<3;s++){auto src=5+s*6;p=add(0x70810004+(s<<16),6);for(unsigned j=2;j<6;j++)v[p+j]=v[src+j];}
 p=add(0x71000013,21);v[p+1]=108*68;v[p+2]=(67<<16)|107;v[p+4]=4;
 for(unsigned s=0;s<slices;s++){unsigned start=68*s/slices,end=68*(s+1)/slices;p=add(0x71030009,11);v[p+4]=(start<<24)|(start*108);v[p+5]=end<<16;v[p+6]=s+1==slices?1<<19:0;p=add(0x70870002,4);v[p+1]=start;v[p+2]=end;}
 return v;
}
static void verify(std::vector<uint32_t> v) {
 auto before=v;Plan p;Reason reason;assert(plan(v.data(),v.size(),p,&reason)&&reason==Reason::Ready&&p.count==3);
 assert(v==before);assert(apply(v.data(),v.size(),p));unsigned changed=0;
 for(unsigned i=0;i<v.size();i++)if(v[i]!=before[i]){++changed;assert((v[i]^before[i])==0x68000000U);assert((v[i]>>27)==4);}
 assert(changed==3);assert(!plan(v.data(),v.size(),p,&reason));
}
int main(int argc,char **argv) {
 for(unsigned slices=1;slices<=16;slices++)verify(batch(slices));
 auto v=batch();Plan p;Reason r;assert(plan(v.data(),v.size(),p,&r));
 for(size_t n=0;n<v.size();++n){Plan truncated;auto saved=v;assert(!plan(v.data(),n,truncated,&r));assert(truncated.count==0&&v==saved);}
 for(unsigned i=0;i<3;i++){auto b=v;auto q=p;b[q.edits[i].index]^=8;auto saved=b;assert(!apply(b.data(),b.size(),q)&&b==saved);q=p;q.edits[i].after^=1;assert(!apply(v.data(),v.size(),q));}
 {auto b=v;auto q=p;q.edits[1]=q.edits[0];assert(!apply(b.data(),b.size(),q)&&b==v);}
 // Every control or geometry rejection must preserve all source words.
 for(unsigned i=0;i<v.size();i++) {
  const bool header=v[i]==0x70000003 || v[i]==0x70800000 || v[i]==0x71000013 || v[i]==0x71030009 || v[i]==0x70870002 || v[i]==0x70010004 || v[i]==0x70810004 || v[i]==0x70820004 || v[i]==0x70830004;
  if(header){auto b=v;b[i]=0xdeadbeef;auto saved=b;assert(!plan(b.data(),b.size(),p,&r)&&p.count==0&&b==saved);}
 }
 // Raw/ref/scaled layout must agree with its MFX counterpart.
 assert(plan(v.data(),v.size(),p,&r));for(unsigned s=0;s<3;s++)for(unsigned j=0;j<4;j++) {
  auto b=v;b[p.edits[s].index-1+j]^=1;auto saved=b;assert(!plan(b.data(),b.size(),p,&r)&&b==saved);
  assert(plan(v.data(),v.size(),p,&r));
 }
 {auto b=v;b[6]=3;assert(!plan(b.data(),b.size(),p,&r)&&r==Reason::SurfaceID);}
 {auto b=v;b[1]=2;assert(!plan(b.data(),b.size(),p,&r)&&r==Reason::Pipe);}
 assert(plan(v.data(),v.size(),p,&r));
 for(unsigned s=0;s<3;s++)for(unsigned kind=0;kind<4;kind++) {
  auto b=v;unsigned vd=p.edits[s].index,mfx=5+s*6+3;
  if(kind==0)b[vd]=b[mfx]=0x480000fb; // pitch too short in both paired states
  if(kind==1)b[vd]=b[mfx]=0x580037fb; // another pixel format
  if(kind==2){b[vd+1]=b[vd+2]=b[mfx+1]=b[mfx+2]=1;} // UV before Y ends
  if(kind==3){b[vd-1]^=16;b[mfx-1]^=16;} // paired but inconsistent image dimensions
  auto saved=b;Plan no;assert(!plan(b.data(),b.size(),no,&r)&&no.count==0&&b==saved);
 }
 // A packet-looking payload word is never interpreted as another surface.
 {auto b=v;b[3]=0x70810004;verify(b);}
 assert(!plan(nullptr,128,p,&r)&&r==Reason::Bounds);assert(!plan(v.data(),262145,p,&r)&&r==Reason::Bounds);
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes{std::istreambuf_iterator<char>(f),{}};assert(!bytes.empty()&&bytes.size()%4==0);std::vector<uint32_t> real(bytes.size()/4);memcpy(real.data(),bytes.data(),bytes.size());verify(real);}
 puts("PASS VDEnc surface planner: coherent progressive 1..16 slices, paired layouts, bounded structural parsing, format-only changes, atomic validation, actual fixture when supplied");
}
