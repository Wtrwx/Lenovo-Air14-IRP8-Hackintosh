#!/usr/bin/env python3
"""Execute the production HDMI timing-copy adapter with synthetic objects."""
from pathlib import Path
import re, subprocess, tempfile
r = Path(__file__).resolve().parents[1]
s = (r / 'WhateverXe/kern_xe.cpp').read_text()
helper = re.search(r'void XE::configureDisplay13HDMISDR\(.*?\n\}', s, re.S).group()
assert 'orgDisplay13HDMIModeTiming' not in s
assert 'hdmiTimingSymbol' not in s
a = s.index('void XE::wrapTGLModeTiming(')
b = s.index('uint32_t XE::wrapTGLPortDDC(', a)
route = s[a:b]
assert route.index('orgTGLModeTiming)(controller,fb,timing)') < route.index('configureDisplay13HDMISDR(fb,timing)') < route.index('TGLPanelDepth::clamp')
code = r'''
#include "kern_display13_hdmi_caps.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
struct Meta {unsigned size=0x5000;unsigned getClassSize(){return size;}};
struct OSObject {alignas(8) unsigned char bytes[0x5000]{};Meta meta;Meta *getMetaClass(){return &meta;}};
using IOService=OSObject;
namespace Display13FBMemory {template<class T> T field(void *p,unsigned o){T v;memcpy(&v,static_cast<unsigned char *>(p)+o,sizeof(v));return v;}}
template<class T> void put(void *p,unsigned o,T v){memcpy(static_cast<unsigned char *>(p)+o,&v,sizeof(v));}
unsigned nativeCalls=0,records=0;uint64_t last[5];
struct Root {void setProperty(const char *,void *p,size_t n){assert(n==sizeof(last));memcpy(last,p,n);++records;}} root;
struct IORegistryEntry {static Root *getRegistryRoot(){return &root;}};
struct XE {
    bool display13FramebufferRequested=true,display13BootReady=true,allowHDMI=true;
    void *adlnTopologyController=nullptr;OSObject *selected=nullptr;
    uintptr_t orgDisplay13HDMIModeTiming=0;
    bool display13HDMIPort(OSObject *p){return allowHDMI && p==selected;}
    static void configureDisplay13HDMISDR(void *,void *);
} state,*callbackXE=&state;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
PRODUCTION
void native(void *,void *,void *){++nativeCalls;}
int main(){
    OSObject fb,path,port;int controller;
    state.adlnTopologyController=&controller;state.selected=&port;
    state.orgDisplay13HDMIModeTiming=reinterpret_cast<uintptr_t>(native);
    put(&fb,0x1D0,static_cast<void *>(&controller));put(&fb,0x3FD8,int32_t(-1));
    put(&fb,0x49F0,static_cast<OSObject *>(&path));put(&path,0x32B0,static_cast<void *>(&fb));
    put(&path,0x3648,static_cast<OSObject *>(&port));
    alignas(8) unsigned char timing[160]{},before[160];
    put(timing,0x88,uint16_t(2));put(timing,0x8A,uint16_t(4));put(timing,0x8E,uint16_t(1));
    memcpy(before,timing,160);XE::configureDisplay13HDMISDR(&fb,timing);
    assert(records==1 && last[3]==4 && last[4]==2);
    put(before,0x8A,uint16_t(2));assert(!memcmp(before,timing,160));
    for(unsigned fail=0;fail<7;++fail){
        state.display13BootReady=fail!=0;state.allowHDMI=fail!=1;
        fb.meta.size=fail==2?0x100:0x5000;path.meta.size=fail==3?0x100:0x5000;
        put(&fb,0x1D0,fail==4?nullptr:static_cast<void *>(&controller));
        put(&path,0x32B0,fail==5?nullptr:static_cast<void *>(&fb));
        state.display13FramebufferRequested=fail!=6;
        put(timing,0x8A,uint16_t(4));auto n=records;
        XE::configureDisplay13HDMISDR(&fb,timing);
        assert(records==n && Display13FBMemory::field<uint16_t>(timing,0x8A)==4);
    }
    assert(Display13HDMI::sdrDepth(1,8,1)==2);
    assert(Display13HDMI::sdrDepth(2,16,1)==2);
    assert(Display13HDMI::sdrDepth(1,2,1)==2);
    assert(Display13HDMI::sdrDepth(1,4,2)==4); // HDR retains its owner.
    assert(Display13HDMI::sdrDepth(4,4,1)==4); // Subsampling has different clock math.
    assert(Display13HDMI::sdrDepth(8,4,1)==4);
    assert(Display13HDMI::sdrDepth(1,3,1)==3); // Capability mask is not a selection.
    puts("PASS production HDMI SDR timing copy: native first, owner/path/port guards, exact depth-only change, HDR/subsampling exclusion. Synthetic objects only.");
}
'''.replace('PRODUCTION', helper)
with tempfile.TemporaryDirectory(prefix='d13-hdmi-mode-') as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I'+str(r/'WhateverXe'), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
