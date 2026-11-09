#!/usr/bin/env python3
"""Production WM admission plus real IRQ stop proof for last-DDI release."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
f=s[s.index('bool XE::adlnPowerReference('):]
admission=re.search(r'!\(Display13WM::busyPipes\(c.pipe,.*?\)&affected\)',f)[0]
fixture=(r/'tests/display13_fb_irq.cpp').read_text().split('int main(')[0]
code=fixture+r'''
#include "kern_display13_wm.hpp"
struct Plan{Display13WM::Status status=Display13WM::Status::Complete;};
struct Pipe{bool wmProgramming=false,wmCommitting=false;unsigned wmPending=0;uint64_t watermarkClockHz=100;Plan wmProgram[2],wmLatch;};
struct Context{Pipe pipe[3];}c;
struct Wiring{bool dual;};struct Callback{Wiring adlnOutputWiring;}cb,*callbackXE=&cb;
namespace Display13FBTopology{bool multiOutputWiring(Wiring w){return w.dual;}}
int main(){
 for(unsigned kind=0;kind<8;++kind){
  c=Context{};c.pipe[1].wmPending=1;cb.adlnOutputWiring.dual=kind!=1;
  IO io;State state;io.r[0x71008]=0xc0000000;assert(enable(io,state,0,NativeGT,3)==Status::Complete);io.r[0x71008]=0;
  if(kind==2)c.pipe[1].wmProgramming=true;
  if(kind==3)c.pipe[1].wmCommitting=true;
  if(kind==4)io.r[0x71008]=0xc0000000;
  if(kind==5)io.r[DSBInterrupt(1,0)]|=1;
  unsigned sw=kind==6;uint32_t hpd=kind==7?12:0;uint32_t affected=2;
  const auto peer=c.pipe[0];io.writes.clear();PowerReleaseScope scope;
  const bool admitted=ADMISSION;
  const bool safe=admitted&&beginPowerRelease(io,state,scope,32,affected,0,sw,0,0,hpd)==Status::Complete;
  assert(safe==(kind==0));
  if(!safe)assert(io.writes.empty()&&!scope.held);
  else {assert(scope.parked==2);assert(endPowerRelease(io,state,scope)==Status::Complete);}
  assert(c.pipe[0].wmPending==peer.wmPending&&c.pipe[0].watermarkClockHz==peer.watermarkClockHz);
 }
}
'''.replace('ADMISSION',admission)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: pending WM permits only stopped multi-output release; active timing, DSB, SW handler, HPD and programming still block without writes')
