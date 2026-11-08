#!/usr/bin/env python3
"""Execute production HDMI preparation, including cache replacement races."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
helper=re.search(r'bool XE::prepareDisplay13HDMISCDC\(.*?\n\}',s,re.S).group()
code=r'''
#include "kern_display13_hdmi_caps.hpp"
#include "kern_display13_hdmi_scdc.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Meta {unsigned getClassSize(){return 0x5000;}};
struct OSObject {alignas(8) unsigned char bytes[0x5000]{};Meta meta;Meta *getMetaClass(){return &meta;}};
namespace Display13FBMemory {template<class T> T field(void *p,unsigned o){T v;memcpy(&v,static_cast<unsigned char *>(p)+o,sizeof(v));return v;}}
template<class T> void put(void *p,unsigned o,T v){memcpy(static_cast<unsigned char *>(p)+o,&v,sizeof(v));}
struct IODetailedTimingInformationV2 {uint64_t pixelClock=583610000;uint16_t pixelEncoding=1,bitsPerColorComponent=2;};
void *tglAUXLock=reinterpret_cast<void *>(1);unsigned critical=0;
void IOSimpleLockLock(void *){assert(!critical);++critical;}
void IOSimpleLockUnlock(void *){assert(critical==1);--critical;}
struct Display13HDMICache {void *port{};Display13HDMI::Caps caps;uint64_t generation{};Display13HDMI::EDIDCache edid;};
Display13HDMICache display13HDMICache[7];
uint64_t record[12];
struct Root {void setProperty(const char *name,void *p,size_t n){if(!strcmp(name,"N97FBHDMISCDCConnection")){assert(n==64);return;}assert(n==sizeof(record));memcpy(record,p,n);}} root;
struct IORegistryEntry {static Root *getRegistryRoot(){return &root;}};
unsigned calls=0,swapAt=0,failAt=0;bool owned=true;uint8_t config=0xa0;
struct XE {
 static bool display13HDMISCDCOwned(void *,void *,void *){return owned;}
 static bool display13HDMISCDCTransfer(void *,void *,void *,bool read,uint8_t reg,uint8_t &value){
  assert(!critical);++calls;
  if(swapAt==calls)++display13HDMICache[1].generation;
  if(failAt==calls)return false;
  if(read)value=reg==1?1:config;else if(reg==0x20)config=value;
  return true;
 }
 static bool prepareDisplay13HDMISCDC(void *,void *,void *,const void *);
};
PRODUCTION
int main(){
 OSObject fb,path,port;int controller;uint32_t cfg[]={1,2,2};
 put(&path,0x3648,&port);put(&port,0x548,static_cast<uint32_t *>(cfg));
 const uint32_t initial=0x90030200;IODetailedTimingInformationV2 timing;
 display13HDMICache[1]={&port,{true,true,true,600000},7};
 put(&path,0x128,uint32_t(0x12345678));put(&path,0x12C,initial);
 assert(XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing));
 assert(Display13FBMemory::field<uint32_t>(&path,0x12C)==(initial|17));
 assert(Display13FBMemory::field<uint32_t>(&path,0x128)==0x12345678 && record[5]==1);
 for(unsigned swap=1;swap<=5;++swap){
  calls=0;swapAt=swap;config=0xa0;put(&path,0x12C,initial);
  assert(!XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing));
  assert(Display13FBMemory::field<uint32_t>(&path,0x12C)==initial);
 }
 swapAt=0;failAt=3;calls=0;put(&path,0x12C,initial);
 assert(!XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing));
 assert(Display13FBMemory::field<uint32_t>(&path,0x12C)==initial);
 failAt=0;calls=0;put(&path,0x12C,uint32_t(0x91030200));timing.pixelClock=148500000;
 assert(XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing) && !calls);
 put(&path,0x12C,uint32_t(0x92030200));
 assert(!XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing) && !calls);
 put(&path,0x12C,initial);display13HDMICache[1].caps={};timing.pixelClock=241570000;
 assert(XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing) && !calls);
 timing.pixelClock=583610000;
 assert(!XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing) && !calls);
 owned=false;assert(!XE::prepareDisplay13HDMISCDC(&controller,&fb,&path,&timing) && !calls);
 puts("PASS production HDMI preparation: 600 MHz path, DVI/DP selection, unchanged clock selector, source flags only after sink verification, stale EDID generations, native transfer failure and ownership loss. Synthetic native I2C only.");
}
'''.replace('PRODUCTION',helper)
with tempfile.TemporaryDirectory(prefix='d13-hdmi-prepare-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
   '-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
