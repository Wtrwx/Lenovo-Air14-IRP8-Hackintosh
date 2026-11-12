#!/usr/bin/env python3
"""Execute production metadata wrappers against bounded CPU object fixtures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
start=source.index('static void tglVideoMapBegin(')
stop=source.index('// One attempt per boot,',start)
body=source[start:stop]
fixture='''
#include "kern_tgl_video_audit.hpp"
#define main standalone_map_main
#include "tgl_video_map.cpp"
#undef main
static Fixture *active;
static uint64_t nofault(uint64_t p,uint64_t out,unsigned n){return active->read(p,reinterpret_cast<void*>(out),n);}
static struct {uint64_t(*nofault)(uint64_t,uint64_t,unsigned)=::nofault;} tglVM;
// PRODUCTION
int main(){
 Fixture f;active=&f;TGLVideoAudit::Record r{};
 r.q[TGLVideoAudit::GPU]=Fixture::G;r.q[TGLVideoAudit::Words]=1024;
 r.alias.q[TGLVideoAlias::BeforeQualified]=1;r.alias.owner[0][TGLVideoAlias::Task]=Fixture::T;
 auto original=f.memory;
 tglVideoMapBegin(r);assert(r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::DeclaredSpan);
 tglVideoMapFinish(r);assert(r.map.q[TGLVideoMap::Status]==TGLVideoMap::DeclaredSpan&&f.memory==original);
 // A changed alias blocks a second traversal of the old task.
 tglVideoMapBegin(r);unsigned reads=f.reads;r.alias.q[TGLVideoAlias::Status]=TGLVideoAlias::OwnerChanged;
 tglVideoMapFinish(r);assert(f.reads==reads&&r.map.q[TGLVideoMap::Status]==TGLVideoMap::Changed);
 // Unknown alias qualification must not default to a native map hit.
 r.alias.q[TGLVideoAlias::BeforeQualified]=0;reads=f.reads;tglVideoMapBegin(r);
 assert(f.reads==reads&&r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::NotChecked);
 // Donor read failure stays unknown and never mutates fixture objects.
 r.alias.q[TGLVideoAlias::BeforeQualified]=1;f.failAt=f.reads+8;tglVideoMapBegin(r);
 assert(r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::ReadFailed&&f.memory==original);
 // A large VCS command receives metadata only after independent ownership.
 f.failAt=0;
 constexpr uint64_t R=Fixture::K+0x600000,C=R+0x1000,O=R+0x2000,
  P=R+0x3000,L=R+0x4000,U=R+0x10000,V=R+0x20000;
 f.put(R+0x20,C);f.put(R+0x10,O);f.put(R+0x40,3,1);f.put(R+0x18,U);
 f.put(C+0x50,O);f.put(C+0xb0,R);f.put(C+0x58,Fixture::T);
 f.put(C+0x98,L);f.put(C+0x7c,7,4);f.put(L+0x38,U);f.put(L+0x20,8192);
 f.put(U+0x10c4,0,4);f.put(U+0x10cc,0x1000,4);
 f.put(Fixture::T+0x260,P);f.put(P,V);
 f.put(Fixture::X+0x40,16384);f.put(Fixture::A+0xc8,16384);
 r={};r.q[TGLVideoAudit::GPU]=Fixture::G;r.q[TGLVideoAudit::Words]=3072;
 r.q[TGLVideoAudit::Ring]=R;r.q[TGLVideoAudit::Context]=C;
 r.q[TGLVideoAudit::ContextID]=7;r.q[TGLVideoAudit::Engine]=3;
 r.alias.q[TGLVideoAlias::ExpectedClass]=V;r.alias.q[TGLVideoAlias::Status]=TGLVideoAlias::Bounds;
 auto largeOriginal=f.memory;
 tglVideoMapBegin(r);assert(r.map.q[TGLVideoMap::Schema]==2&&r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::DeclaredSpan);
 tglVideoMapFinish(r);assert(r.map.q[TGLVideoMap::Status]==TGLVideoMap::DeclaredSpan);
 assert(r.alias.q[TGLVideoAlias::BeforeQualified]==0&&r.alias.q[TGLVideoAlias::Status]==TGLVideoAlias::Bounds);
 assert(f.memory==largeOriginal);
 // Ownership drift blocks finish and cannot authorize an alias or edits.
 tglVideoMapBegin(r);f.put(C+0x58,Fixture::T+8);reads=f.reads;
 tglVideoMapFinish(r);assert(r.map.q[TGLVideoMap::Status]==TGLVideoMap::Changed);
 assert(r.alias.q[TGLVideoAlias::BeforeQualified]==0);f.put(C+0x58,Fixture::T);
 // A foreign table class prevents traversal before any map read.
 f.put(P,V+8);tglVideoMapBegin(r);assert(r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::NotChecked);
 f.put(P,V);r.q[TGLVideoAudit::ContextID]=8;tglVideoMapBegin(r);
 assert(r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::NotChecked);
 // Rejected first-node extents retain their actual fields for diagnosis.
 r.q[TGLVideoAudit::ContextID]=7;f.put(Fixture::A+0xc8,0);
 tglVideoMapBegin(r);assert(r.map.q[TGLVideoMap::BeforeStatus]==TGLVideoMap::MapBounds);
 assert(r.map.samples[0].node[TGLVideoMap::Address]==Fixture::A);
 assert(r.map.samples[0].node[TGLVideoMap::AssignedLength]==0);
 puts("PASS production map wrappers: exact reader, alias admission/change, failed donor read, no target resource read or writes");
}
'''.replace('// PRODUCTION',body)
with tempfile.TemporaryDirectory(prefix='wex-video-map-hook-') as d:
 d=Path(d);(d/'test.cpp').write_text(fixture)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                 '-I'+str(root/'WhateverXe'),'-I'+str(root/'tests'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
