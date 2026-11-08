#!/usr/bin/env python3
"""Execute the production SCDC adapter against a synthetic native I2C gate."""
from pathlib import Path
import re, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
helper=re.search(r'bool XE::display13HDMISCDCTransfer\(.*?\n\}',s,re.S).group()
a=s.index('uint32_t XE::wrapTGLLightUpTMDS(')
b=s.index('// TGL-U has Combo PHY',a)
assert s[a:b].index('prepareDisplay13HDMISCDC(')<s[a:b].index('orgTGLLightUpTMDS)(controller')
assert 'static_cast<uint8_t *>(path)+0x12C' in s
code=r'''
#include "kern_display13_hdmi_scdc.hpp"
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
struct OSObject {alignas(8) unsigned char bytes[0x600]{};};
namespace Display13FBMemory {template<class T> T field(void *p,unsigned o){T v;memcpy(&v,static_cast<unsigned char *>(p)+o,sizeof(v));return v;}}
template<class T> void put(void *p,unsigned o,T v){memcpy(static_cast<unsigned char *>(p)+o,&v,sizeof(v));}
#pragma pack(push,4)
struct IOI2CRequest {
 uint32_t sendTransactionType,replyTransactionType,sendAddress,replyAddress;
 uint8_t sendSubAddress,replySubAddress,reservedA[2];uint64_t minReplyDelay;
 uint32_t result,commFlags,padA,sendBytes,reservedB[2],padB,replyBytes;
 void *completion;uintptr_t sendBuffer,replyBuffer;uint32_t reservedC[10];
};
#pragma pack(pop)
struct IOI2CBusTiming {};
uint64_t lastObservation[11] {};
struct Root {void setProperty(const char *key,void *value,size_t size){assert(!strcmp(key,"N97FBHDMISCDCTransfer") && size==sizeof(lastObservation));memcpy(lastObservation,value,size);}} root;
struct IORegistryEntry {static Root *getRegistryRoot(){return &root;}};
constexpr uint32_t kIOReturnNotReady=0xe00002d8;
bool owned=true,reading=true;unsigned calls=0,fail=0;
uint32_t native(void *ctl,uint32_t fb,uint32_t pin,uint32_t speed,IOI2CBusTiming *timing,IOI2CRequest *q){
 ++calls;assert(ctl && fb==7 && pin==2 && !speed && !timing);
 assert(!q->completion && !q->minReplyDelay && !q->reservedC[9]);
 if(reading){
  assert(q->sendTransactionType==1 && q->sendAddress==0xa8 && q->sendBytes==1);
  assert(*reinterpret_cast<uint8_t *>(q->sendBuffer)==0x20);
  assert(q->replyTransactionType==1 && q->replyAddress==0xa8);
  assert(!q->replySubAddress && !q->commFlags && q->replyBytes==1);
  *reinterpret_cast<uint8_t *>(q->replyBuffer)=0xa3;
  if(fail==3)q->replyBytes=0;
  if(fail==5)q->sendBytes=0;
 }else{
  assert(q->sendTransactionType==1 && !q->replyTransactionType && q->sendAddress==0xa8 && !q->commFlags);
  auto b=reinterpret_cast<const uint8_t *>(q->sendBuffer);assert(q->sendBytes==2 && b[0]==0x20 && b[1]==0xa3);
  if(fail==3)q->sendBytes=1;
 }
 q->result=fail==2?1:0;if(fail==4)owned=false;return fail==1?1:0;
}
struct XE {
 uintptr_t display13HDMII2CRequest=reinterpret_cast<uintptr_t>(native);
 static bool display13HDMISCDCOwned(void *,void *,void *){return owned;}
 static bool display13HDMISCDCTransfer(void *,void *,void *,bool,uint8_t,uint8_t &);
} state,*callbackXE=&state;
PRODUCTION
int main(){
 OSObject fb,port;int controller;uint32_t cfg[]={1,2,2};
 put(&fb,0x1DC,uint32_t(7));
 // Path storage must match the donor field offsets, not the small port.
 alignas(8) unsigned char pathBytes[0x3650]{};
 put(pathBytes,0x3648,&port);put(&port,0x548,static_cast<uint32_t *>(cfg));
 for(bool read:{true,false})for(unsigned error=0;error<=(read?5U:4U);++error){
  owned=true;reading=read;fail=error;uint8_t value=read?0:0xa3;
  assert(XE::display13HDMISCDCTransfer(&controller,&fb,pathBytes,read,0x20,value)==(error==0));
  assert(lastObservation[3]==read && lastObservation[4]==0x20);
  assert(lastObservation[5]==(error==1) && lastObservation[6]==(error==2) && lastObservation[10]==(error!=4));
 }
 owned=false;auto count=calls;uint8_t value=0;
 assert(!XE::display13HDMISCDCTransfer(&controller,&fb,pathBytes,true,0x20,value) && calls==count);
 puts("PASS production SCDC I2C: native gate arguments, donor request ABI, combined register-pointer read/write, dual error status, partial transfer and ownership loss. Synthetic native gate only.");
}
'''.replace('PRODUCTION',helper)
with tempfile.TemporaryDirectory(prefix='d13-hdmi-i2c-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
   '-fno-sanitize-recover=all','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
