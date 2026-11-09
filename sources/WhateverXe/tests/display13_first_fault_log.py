#!/usr/bin/env python3
"""Exercise the production first-fault record when later failures cascade."""
from pathlib import Path
import re, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_xe.cpp').read_text()
function = re.search(r'static void adlnFault\(.*?\n\}', source, re.S).group()
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
struct IOService {
 IOService *getMetaClass(){return this;}
 unsigned getClassSize(){return 0;}
};
namespace Display13FBMemory {
 template<class T>T field(void*,unsigned){assert(false);return {};}
}
struct Power {
 uint32_t firstFaultLine{},sequence{7};
 bool memoryFaulted{},clockFaulted{},watermarkFaulted{},engineEnabled{true},clockStopped{};
 struct Bridge {
  struct State {bool faulted{},suspended{};unsigned owned{},borrowed{};}state;
  uintptr_t bootPath{};bool begun{};unsigned pgRefs[3]{};
 }bridge;
 struct DBUF {bool faulted{};unsigned selected{3},consumers{15},unretired{},updating{};}dbuf;
 struct Pipe {
  IOService *memoryFramebuffer{};uintptr_t publishedScanout{};
  unsigned wmPending{};bool wmProgramming{},wmCommitting{},destroyingPath{};
 }pipe[3];
 IOService *controller{};
}display13FBPower;
struct {unsigned handlers{};bool quiescing{};}display13FBIRQ;
static std::vector<std::string> logs;
static uint64_t record[32];static unsigned publications;
struct IORegistryEntry {
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry root;return &root;}
 void setProperty(const char *key,const void *data,size_t bytes){
  assert(std::string(key)=="N97FBFirstResourceFault" && bytes==sizeof(record));
  memcpy(record,data,bytes);++publications;
 }
};
void clock_get_uptime(uint64_t *t){*t=123;}
void absolutetime_to_nanoseconds(uint64_t t,uint64_t *out){*out=t;}
void logMessage(const char *format,...){
 char text[512];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);
 logs.emplace_back(text);
}
#define SYSLOG(module,format,...) logMessage(format,##__VA_ARGS__)
// FUNCTION
int main(){
 for(unsigned category=1;category<=5;++category){
  display13FBPower={};logs.clear();publications=0;
  auto &c=display13FBPower;
  c.pipe[1].memoryFramebuffer=reinterpret_cast<IOService*>(uintptr_t(0x12345678));
  c.pipe[1].publishedScanout=0xfeedbeef;c.pipe[1].wmPending=2;
  bool *target=category==1?&c.memoryFaulted:category==2?&c.clockFaulted:
   category==3?&c.dbuf.faulted:category==4?&c.watermarkFaulted:&c.bridge.state.faulted;
  adlnFault(*target,9001,42);
  assert(*target && publications==1 && logs.size()==4);
  assert(record[1]==9001 && record[2]==42 && record[3]==7);
  assert(logs[0].find("line=9001 status=42 category="+std::to_string(category))!=std::string::npos);
  assert(logs[2].find("pipe=1 bound=1 scanout=1 wmPending=2")!=std::string::npos);
  for(const auto &log:logs){assert(log.find("12345678")==std::string::npos);assert(log.find("feedbeef")==std::string::npos);}
  adlnFault(c.memoryFaulted,9999);adlnFault(c.bridge.state.faulted,9998,1);
  assert(c.memoryFaulted && c.bridge.state.faulted && publications==1 && logs.size()==4);
  assert(record[1]==9001 && c.firstFaultLine==9001);
 }
 puts("production first fault retained once across cascade; five categories and all pipes passed");
}
'''.replace('// FUNCTION', function)
with tempfile.TemporaryDirectory(prefix='display-first-fault-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
