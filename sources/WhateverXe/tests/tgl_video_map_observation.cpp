#define TGL_INTEL_MAP_FIXTURE_ONLY
#include "tgl_video_intel_map.cpp"
#undef TGL_INTEL_MAP_FIXTURE_ONLY
#include <type_traits>
static_assert(!std::is_convertible<TGLVideoMap::Observation,TGLVideoMap::Receipt>::value,
              "a survey must not become an edit-admission receipt");
int main() {
 using namespace TGLVideoMap;
 // Captured failure stopped at an unprepared, segmented peer with zero
 // reservation and a disjoint native span. Keep strict admission unchanged.
 IntelFixture f;f.put(Fixture::A+0xc8,0);f.put(Fixture::A+0xc,0,4);
 f.put(Fixture::A+0xb0,1);f.put(Fixture::A+0x118,0,4);
 const auto original=f.memory;Receipt strict;
 assert(!f.beginReceipt(strict,Fixture::G+0x10000,1));
 assert(strict.q[BeforeStatus]==MapBounds);
 auto rd=[&](uint64_t p,void *d,unsigned n){return f.read(p,d,n);};
 Observation out;f.reads=0;observe(out,Fixture::T,Fixture::G+0x10000,1,rd);
 assert(out.metadata.q[0]==ObservedMetadata&&out.metadata.q[3]==1);
 assert(out.metadata.node[Address]==Fixture::B&&f.memory==original);
 assert(out.metadata.q[0]!=DeclaredSpan);const auto reads=f.reads;
 // Every missing or partial metadata read prevents a successful survey.
 for(bool partial:{false,true})for(unsigned fail=1;fail<=reads;++fail) {
  f.memory=original;f.reads=0;f.failAt=fail;f.shortRead=partial;
  observe(out,Fixture::T,Fixture::G+0x10000,1,rd);
  assert(out.metadata.q[0]!=ObservedMetadata&&f.memory==original);
 }
 f.failAt=0;f.shortRead=false;
 // A zero-reservation node containing the target is still rejected.
 observe(out,Fixture::T,Fixture::G,1,rd);assert(out.metadata.q[0]==RangeOverrun);
 for(unsigned bad=0;bad<8;++bad) {
  f.memory=original;
  if(bad==0)f.put(Fixture::B+0xc8,0);
  if(bad==1)f.put(Fixture::B+0xc,0,4);
  if(bad==2)f.put(Fixture::B+0xb0,1);
  if(bad==3)f.put(Fixture::B+0x90,Fixture::T+8);
  if(bad==4)f.put(Fixture::B+0x98,Fixture::G);
  if(bad==5)f.put(Fixture::B+0x28,Fixture::A);
  if(bad==6)f.put(Fixture::A+0x118,255U<<17,4); // peer now overlaps target
  if(bad==7)f.put(IntelFixture::I+40,0,1);
  const auto before=f.memory;observe(out,Fixture::T,Fixture::G+0x10000,1,rd);
  assert(out.metadata.q[0]!=ObservedMetadata&&f.memory==before);
 }
 // A valid overlap can never be silently chosen over the original match.
 f.memory=original;f.put(Fixture::A+0xc8,0x20000);
 f.put(Fixture::A+0x118,32U<<17,4);f.put(Fixture::A+0xc,1,4);f.put(Fixture::A+0xb0,0);
 observe(out,Fixture::T,Fixture::G+0x10000,1,rd);assert(out.metadata.q[0]==Ambiguous);
 printf("PASS separate metadata survey: %u read failures x2, strict admission unchanged, target/overlap/owner/donor guards, no writes\n",reads);
}
