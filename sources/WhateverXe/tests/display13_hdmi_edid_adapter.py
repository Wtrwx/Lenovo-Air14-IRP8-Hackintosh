#!/usr/bin/env python3
"""Exercise the production DDC cache and timing-range adapter together."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
functions='\n'.join(re.search(r'uint32_t XE::'+name+r'\(.*?\n\}',s,re.S).group() for name in ['wrapDisplay13HDMIOnline','wrapAlderLakeNPortDDC'])
code=r'''
#include "kern_display13_hdmi_caps.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdint>
struct Meta {unsigned getClassSize(){return 0x5000;}};
struct OSObject {alignas(8) unsigned char bytes[0x5000]{};Meta meta;Meta *getMetaClass(){return &meta;}};
using IOService=OSObject;
namespace Display13FBMemory {template<class T> T field(void *p,unsigned o){T v;memcpy(&v,static_cast<unsigned char *>(p)+o,sizeof(v));return v;}}
template<class T> void put(void *p,unsigned o,T v){memcpy(static_cast<unsigned char *>(p)+o,&v,sizeof(v));}
namespace Display13FBTopology {bool selected(int,const uint32_t *cfg,int){return cfg && cfg[0]==1;}}
void *tglAUXLock=reinterpret_cast<void *>(1);unsigned critical=0;
void IOSimpleLockLock(void *){assert(!critical);++critical;}
void IOSimpleLockUnlock(void *){assert(critical==1);--critical;}
struct Display13HDMICache {void *port{};Display13HDMI::Caps caps;uint64_t generation{};Display13HDMI::EDIDCache edid;};
Display13HDMICache display13HDMICache[7];
struct Root {void setProperty(const char *,const void *,size_t){}} root;
struct IORegistryEntry {static Root *getRegistryRoot(){return &root;}};
void cachePanelDepth(OSObject *,uint32_t,uint8_t *,uint32_t){}
uint8_t edid[8][128]{};unsigned reads[9]{},failBlock=0;bool portSelected=true;uint32_t onlineStatus=0;
uint32_t nativeDDC(OSObject *,uint32_t,uint8_t *,bool);
uint32_t nativeOnline(IOService *,OSObject *,uint8_t *,uint8_t *);
struct XE {
 void *adlnTopologyController{};int adlnPlatform{},adlnOutputWiring{};
 bool display13HDMII2CRequest=true,display13FBOutputHooksReady=true;
 decltype(&nativeDDC) orgAlderLakeNPortDDC=nativeDDC;
 decltype(&nativeOnline) orgDisplay13HDMIOnline=nativeOnline;
 bool display13HDMIPort(void *){return portSelected;}
 static uint32_t wrapAlderLakeNPortDDC(OSObject *,uint32_t,uint8_t *,bool);
 static uint32_t wrapDisplay13HDMIOnline(IOService *,OSObject *,uint8_t *,uint8_t *);
};
XE xe,*callbackXE=&xe;
#define FunctionCast(name,original) (original)
PRODUCTION
uint32_t nativeDDC(OSObject *,uint32_t block,uint8_t *out,bool){
 assert(!critical && block>=1 && block<=8);++reads[block];
 if(failBlock==block)return 1;
 memcpy(out,edid[block-1],128);return 0;
}
uint32_t nativeOnline(IOService *fb,OSObject *path,uint8_t *online,uint8_t *){
 *online=1;put(fb,0x4368,uint64_t(300000000));
 if(onlineStatus)return onlineStatus;
 auto port=Display13FBMemory::field<OSObject *>(path,0x3648);uint8_t bytes[128];
 XE::wrapAlderLakeNPortDDC(port,1,bytes,false);
 if(edid[0][126])XE::wrapAlderLakeNPortDDC(port,2,bytes,false);
 return 0;
}
void seal(uint8_t *b){b[127]=0;unsigned sum=0;for(unsigned i=0;i<128;++i)sum+=b[i];b[127]=uint8_t(-sum);}
int main(){
 OSObject fb,path,port;int controller;uint32_t cfg[]={1,2,2};
 xe.adlnTopologyController=&controller;put(&fb,0x1D0,static_cast<void *>(&controller));
 put(&path,0x3648,&port);put(&port,0x548,static_cast<uint32_t *>(cfg));
 uint8_t base[]={0,255,255,255,255,255,255,0};memcpy(edid[0],base,8);edid[0][126]=3;seal(edid[0]);
 uint8_t legacy[]={2,3,12,0,0x67,3,12,0,0,0,0,68};memcpy(edid[1],legacy,sizeof(legacy));seal(edid[1]);
 edid[2][0]=0x70;seal(edid[2]);
 uint8_t forum[]={2,3,12,0,0x67,0xd8,0x5d,0xc4,1,120,0x80,0};memcpy(edid[3],forum,sizeof(forum));seal(edid[3]);
 uint8_t online=0,changed=0;
 assert(!XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed));
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==600000000);
 for(unsigned i=1;i<=4;++i)assert(reads[i]==1);for(unsigned i=5;i<=8;++i)assert(!reads[i]);
 // Same-port monitor replacement: its new one-extension EDID removes 600 MHz.
 edid[0][126]=1;seal(edid[0]);memset(reads,0,sizeof(reads));
 assert(!XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed));
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==340000000 && !reads[3]);
 // Failed extra block invalidates capabilities, retaining the native limit.
 edid[0][126]=3;seal(edid[0]);failBlock=3;
 assert(!XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed));
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==300000000 && !display13HDMICache[1].port);
 failBlock=0;edid[0][126]=255;seal(edid[0]);memset(reads,0,sizeof(reads));
 assert(!XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed) && reads[8]==1);
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==600000000);
 // Native failures and ports outside the HDMI selection do not gain a cap.
 onlineStatus=7;assert(XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed)==7);
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==300000000);
 onlineStatus=0;portSelected=false;
 assert(!XE::wrapDisplay13HDMIOnline(&fb,&path,&online,&changed));
 assert(Display13FBMemory::field<uint64_t>(&fb,0x4368)==300000000);
 puts("PASS production HDMI EDID adapters: bounded native reads, later Forum capabilities, port replacement, native failures and HDMI identity guards. Synthetic native DDC only.");
}
'''.replace('PRODUCTION',functions)
with tempfile.TemporaryDirectory(prefix='d13-hdmi-edid-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
  '-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
