#!/usr/bin/env python3
"""Exercise production update admission against stale framebuffer/path owners."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
helper=re.search(r'bool XE::adlnWMUpdateAssociated\(.*?\n\}',s,re.S)[0].replace('bool XE::','static bool ')
arm=re.search(r'void XE::display13WMArmUpdate\(.*?\n\}',s,re.S)[0]
admit=re.search(r'owner=associated &&.*?if\(owner\)c.pipe\[pipe\].wmCommitting=true;',arm,re.S)[0]
code=r'''
#include <cassert>
#include <cstdint>
struct Meta {unsigned getClassSize(){return 0x4b90;}};
struct OSObject{};struct IOService:OSObject{Meta m;Meta*getMetaClass(){return &m;}};
namespace TGLFBBinding {enum class Status{Invalid,Complete};struct Binding{Status status;unsigned pipe;};}
struct Pipe {unsigned wmPending=1;bool wmCommitting=false;};
struct Context {Pipe pipe[3];void*controller=(void*)1;bool watermarkFaulted=false,memoryFaulted=false,clockFaulted=false,engineEnabled=true;void *map=(void*)2,*access=(void*)3;struct {struct {bool faulted=false;}state;}bridge;struct{bool updating=false,faulted=false;}dbuf;}display13FBPower;
struct Callback {int adlnPlatform=0,adlnOutputWiring=0;}callback,*callbackXE=&callback;
unsigned livePipe=1;bool associations=true,writer=true;uint8_t destroyed=0;unsigned inspected=0;
TGLFBBinding::Binding binding{TGLFBBinding::Status::Complete,1};
namespace Display13FBMemory {unsigned boundPipe(Pipe*,IOService*){return livePipe;}template<class T>T field(IOService*,unsigned offset){assert(offset==0x4a48);return destroyed;}}
TGLFBBinding::Binding adlnInspectPathBinding(IOService*,OSObject*,int,int){++inspected;return binding;}
bool display13FBAssociations(void*,unsigned,IOService*,unsigned,void*){return associations;}
void *display13FBExpectedOwner(void*p){return p;}
bool adlnWriterMappingBound(){return writer;}
HELPER
bool attempt(IOService*fb,unsigned pipe,OSObject*path){
 auto &c=display13FBPower;bool owner=false;
 const bool associated=adlnWMUpdateAssociated(fb,pipe,path);
 if(pipe>=3)return false;
 ADMIT
 return owner;
}
int main(){
 IOService fb;OSObject path;
 for(unsigned test=0;test<12;++test){
  display13FBPower=Context{};livePipe=1;binding={TGLFBBinding::Status::Complete,1};associations=writer=true;destroyed=0;inspected=0;
  IOService*target=&fb;unsigned pipe=1;
  if(test==1)binding.status=TGLFBBinding::Status::Invalid;
  if(test==2)binding.pipe=0;
  if(test==3)associations=false;
  if(test==4)destroyed=1;
  if(test==5)writer=false;
  if(test==6)livePipe=0;
  if(test==7)pipe=3;
  if(test==8)target=nullptr;
  if(test==9)display13FBPower.watermarkFaulted=true;
  if(test==10)display13FBPower.dbuf.updating=true;
  if(test==11)display13FBPower.pipe[1].wmPending=0;
  assert(attempt(target,pipe,&path)==(test==0));
  for(unsigned p=0;p<3;++p)assert(display13FBPower.pipe[p].wmCommitting==(test==0&&p==1));
  assert(display13FBPower.watermarkFaulted==(test==9));
  assert(display13FBPower.pipe[1].wmPending==(test==11?0U:1U));
  if(test>=6&&test<=8)assert(inspected==0);
 }
}
'''.replace('HELPER',helper).replace('ADMIT',admit)
with tempfile.TemporaryDirectory(prefix='wm-update-owner-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: production update admission rejects stale/foreign bindings without committing or changing fault/pending state')
