#!/usr/bin/env python3
"""Run the production VBL callback against detached paths and real owners."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'WhateverXe/kern_xe.cpp').read_text()
callback=re.search(r'void XE::wrapDisplay13FBConfigureVBL\(.*?\n\}',s,re.S)[0].replace('void XE::','void ')
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
struct Meta {unsigned size=0x5000;unsigned getClassSize(){return size;}};
struct OSObject {Meta m;Meta*getMetaClass(){return &m;}};struct IOService:OSObject{};
struct PipeState {bool begun=true,enabled=true,faulted=false;unsigned active=3;int transcoders=0;};
namespace Display13FBIRQ {enum class Status{Complete,Power,Topology};}
struct Context {void *lock=(void*)1,*base=(void*)2;PipeState state;bool quiescing=false;unsigned blockedPipes=0,admittedPipes=3;Display13FBIRQ::Status status=Display13FBIRQ::Status::Complete;}display13FBIRQ;
struct Wiring{unsigned typeC=0;unsigned hotplug(){return 3;}operator int()const{return 0;}};
struct Callback{Wiring adlnOutputWiring;int adlnPlatform=0;void*adlnTopologyController=nullptr;}cb,*callbackXE=&cb;
struct Display13FBIRQIO {void *p;uint32_t read(unsigned){return 1;}};
unsigned locks=0,unlocks=0,writes=0,cache=0,currentPipe=1;bool associated=true,badHardware=false;void *pathOwner=nullptr;
unsigned IOSimpleLockLockDisableInterrupt(void*){++locks;return 99;}
void IOSimpleLockUnlockEnableInterrupt(void*,unsigned v){assert(v==99);++unlocks;}
bool adlnIRQOwner(IOService*){return true;}
bool display13FBAssociations(IOService*,unsigned,IOService*,unsigned,void*){return associated;}
void*display13FBExpectedOwner(IOService*p){return p;}
namespace Display13FBMemory {template<class T>T field(OSObject*,unsigned offset){if(offset==0x32b0)return (T)(uintptr_t)pathOwner;assert(offset==0x3640);return (T)(uintptr_t)currentPipe;}}
namespace Display13FBTopology {bool multiOutputWiring(int){return false;}unsigned excludedHotplug(int,unsigned,int){return 0;}}
unsigned adlnSharedGT(IOService*){return 0;}
namespace Display13Transcoder {bool running(Display13FBIRQIO&,unsigned,int){return true;}}
namespace Display13FBIRQ {
constexpr unsigned Driver=0;bool powered(unsigned,unsigned){return true;}
Status fail(Display13FBIRQIO&,PipeState&s,Status why){s.faulted=true;++writes;return why;}
Status vblank(Display13FBIRQIO&io,PipeState&s,unsigned,bool){return badHardware?fail(io,s,Status::Power):(++writes,Status::Complete);}
Status enable(Display13FBIRQIO&,PipeState&,unsigned,unsigned,unsigned,unsigned,unsigned){return Status::Complete;}
Status extendActivePipes(Display13FBIRQIO&,PipeState&,unsigned){return Status::Complete;}
}
void adlnIRQCache(IOService*,Display13FBIRQIO&){++cache;}
CALLBACK
int main(){
 IOService controller,fb,foreign;OSObject path;
 for(unsigned test=0;test<7;++test)for(bool getLock:{false,true})for(bool enable:{false,true}){
  display13FBIRQ=Context{};locks=unlocks=writes=cache=0;currentPipe=1;associated=true;badHardware=false;pathOwner=&fb;path.m.size=0x5000;
  if(test==1)associated=false;
  if(test==2)pathOwner=&foreign;
  if(test==3)path.m.size=0x100;
  if(test==4)currentPipe=UINT32_MAX;
  if(test==5)currentPipe=3;
  if(test==6)badHardware=true;
  auto before=display13FBIRQ;
  wrapDisplay13FBConfigureVBL(&controller,&fb,&path,enable,getLock);
  assert(locks==unsigned(getLock)&&unlocks==unsigned(getLock));
  if(test>=1&&test<=5){assert(!writes&&!cache);assert(!memcmp(&before,&display13FBIRQ,sizeof(before)));}
  else {assert(writes==1&&cache==1);assert(display13FBIRQ.state.faulted==badHardware);}
 }
}
'''.replace('CALLBACK',callback).replace('#include <cstring>','#include <cstring>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: 28 production VBL callback cases; detached owners leave IRQ state/MMIO unchanged; owned hardware failures remain strict')
