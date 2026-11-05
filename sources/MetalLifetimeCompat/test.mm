// SPDX-License-Identifier: MIT
#include "lifetime.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <sys/mman.h>

using namespace metal_lifetime;
static uint8_t *code;
static std::atomic<unsigned> deletes {0};
static constexpr const char *TargetType =
    "^{IGPoshUsedAsTarget={IGPoshResourceUsedAsTarget=[1024I][1024[4Q]]B}"
    "{IGPoshResourceUsedAsTarget=[1024I][1024[4Q]]B}}";
extern "C" void metal_lifetime_replay(id, const void *);
static void trackedDelete(void *pointer) { ++deletes; std::free(pointer); }
static void nativeFragment(id object, SEL) { metal_lifetime_replay(object, code+0x7ed6b); }
static Class addClass(const char *name, Class super) {
    Class cls=objc_allocateClassPair(super,name,0); assert(cls); return cls;
}
static void pad(Class cls, const char *name, size_t size) {
    assert(class_addIvar(cls,name,size,0,"[1C]"));
}
static std::pair<Class,Class> fixture() {
    // A standalone root keeps this test independent of Foundation/Metal and
    // of any system classes that those frameworks register automatically.
    Class root=addClass("MLCTestRoot",Nil);
    assert(class_addIvar(root,"isa",8,3,"#"));objc_registerClassPair(root);
    Class base=addClass("MTLIOAccelCommandBuffer",root);
    pad(base,"_superGrowth",760); objc_registerClassPair(base);
    Class buffer=addClass("MTLIGAccelCommandBuffer",base);
    assert(class_addIvar(buffer,"_pIGCommandQueue",8,3,"@\"MTLIGAccelCommandQueue\""));
    pad(buffer,"_unrelated1",40);
    assert(class_addIvar(buffer,"_pPoshUsedAsTarget",8,3,TargetType));
    pad(buffer,"_unrelated2",56);
    assert(class_addIvar(buffer,"m_isOnlyActiveCommandBuffer",1,0,"B"));
    pad(buffer,"_tail",39);
    assert(class_addMethod(buffer,sel_registerName("dealloc"),reinterpret_cast<IMP>(nativeFragment),"v16@0:8"));
    objc_registerClassPair(buffer);
    Class queueBase=addClass("MTLIOAccelCommandQueue",root);
    pad(queueBase,"_superGrowth",864); objc_registerClassPair(queueBase);
    Class queue=addClass("MTLIGAccelCommandQueue",queueBase);
    pad(queue,"_unrelated",136);
    assert(class_addIvar(queue,"m_pPoshUsedAsTarget",8,3,TargetType));
    objc_registerClassPair(queue); return {buffer,queue};
}
static Layout layout;
struct Objects {
    id buffer,queue;
    void *target;
    Objects(uint8_t sole, Class cb=Nil, Class cq=Nil) {
        buffer=class_createInstance(cb?cb:layout.buffer,0);
        queue=class_createInstance(cq?cq:layout.queue,0);
        assert(buffer && queue); target=std::malloc(0x12010); assert(target);
        std::memset(target,0xa5,0x12010);
        store<id>((void *)buffer,layout.queueOffset,queue);
        store<void *>((void *)buffer,layout.targetOffset,target);
        store<uint8_t>((void *)buffer,layout.soleOffset,sole);
        store<void *>((void *)queue,layout.cacheOffset,nullptr);
    }
    void invoke() {
        auto fn=reinterpret_cast<Dealloc>(method_getImplementation(
            class_getInstanceMethod(object_getClass(buffer),sel_registerName("dealloc"))));
        fn(buffer,sel_registerName("dealloc"));
    }
    ~Objects() {
        void *remaining=load<void *>((void *)buffer,layout.targetOffset);
        if(remaining)std::free(remaining);
        object_dispose(buffer);object_dispose(queue);
    }
};
static void mapDonor(const char *path) {
    std::ifstream file(path,std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    assert(bytes.size()==5314944);
    unsigned char digest[32];char text[65] {};
    CC_SHA256(bytes.data(),static_cast<CC_LONG>(bytes.size()),digest);
    for(size_t i=0;i<sizeof(digest);++i)std::snprintf(text+2*i,3,"%02x",digest[i]);
    assert(!std::strcmp(text,"1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c"));
    code=static_cast<uint8_t *>(mmap(nullptr,bytes.size(),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(code!=MAP_FAILED);std::memcpy(code,bytes.data(),bytes.size());
    // Only the native destructor's target-table fragment executes. Its delete
    // stub is redirected to our tracked allocator; no Apple driver is loaded.
    assert(code[0x7ed96]==0x48);code[0x7ed96]=0xc3;
    uint8_t jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    auto address=reinterpret_cast<uintptr_t>(trackedDelete);
    std::memcpy(jump+2,&address,8);std::memcpy(code+0x9e808,jump,sizeof(jump));
    store<ptrdiff_t>(code,0x499eb0,layout.targetOffset);
    store<ptrdiff_t>(code,0x499ea8,layout.soleOffset);
    assert(mprotect(code,bytes.size(),PROT_READ|PROT_EXEC)==0);
}
int main(int argc,char **argv) {
    assert(argc==2);auto classes=fixture();assert(inspect(classes.first,classes.second,layout));
    assert(layout.queueOffset!=600 && layout.cacheOffset!=816);
    Layout unchanged=layout;assert(!inspect(Nil,classes.second,unchanged));
    assert(unchanged.buffer==layout.buffer && unchanged.targetOffset==layout.targetOffset);
    Class bad=addClass("WrongTargetTableType",class_getSuperclass(classes.first));
    assert(class_addIvar(bad,"_pIGCommandQueue",8,3,"@\"MTLIGAccelCommandQueue\""));
    pad(bad,"_padding",40);assert(class_addIvar(bad,"_pPoshUsedAsTarget",8,3,"^v"));
    pad(bad,"_padding2",56);assert(class_addIvar(bad,"m_isOnlyActiveCommandBuffer",1,0,"B"));
    objc_registerClassPair(bad);assert(!inspect(bad,classes.second,unchanged));mapDonor(argv[1]);
    { Objects o(1);nativeFragment(o.buffer,sel_registerName("dealloc"));
      assert(deletes==0 && load<void *>((void *)o.buffer,layout.targetOffset)==o.target); }
    assert(!installForTest(layout,reinterpret_cast<Dealloc>(uintptr_t(1))));
    assert(installForTest(layout,nativeFragment));assert(!installForTest(layout,nativeFragment));
    { Objects o(1);assert(load<uint8_t>((void *)o.buffer,layout.soleOffset)==1);
      for(size_t i=0;i<0x12010;++i)assert(static_cast<uint8_t *>(o.target)[i]==0xa5);
      o.invoke();assert(deletes==1 && repairsForTest()==1);
      assert(!load<void *>((void *)o.buffer,layout.targetOffset));
      assert(!load<void *>((void *)o.queue,layout.cacheOffset)); }
    { Objects o(0);o.invoke();assert(deletes==2 && repairsForTest()==1); }
    { Objects o(1);store<void *>((void *)o.queue,layout.cacheOffset,o.target);o.invoke();
      assert(deletes==2 && repairsForTest()==1);
      assert(load<void *>((void *)o.buffer,layout.targetOffset)==o.target);
      assert(load<uint8_t>((void *)o.buffer,layout.soleOffset)==1); }
    { Objects o(1);store<void *>((void *)o.queue,layout.cacheOffset,reinterpret_cast<void *>(uintptr_t(16)));
      o.invoke();assert(deletes==2 && repairsForTest()==1); }
    { Objects o(1);std::free(o.target);store<void *>((void *)o.buffer,layout.targetOffset,nullptr);
      o.invoke();assert(deletes==2 && repairsForTest()==1); }
    { Objects o(1);store<id>((void *)o.buffer,layout.queueOffset,nil);
      o.invoke();assert(deletes==2 && repairsForTest()==1); }
    { Objects o(2);o.invoke();assert(deletes==2 && repairsForTest()==1); }
    Class sub=addClass("UnknownBufferSubclass",layout.buffer);objc_registerClassPair(sub);
    Class subQueue=addClass("UnknownQueueSubclass",layout.queue);objc_registerClassPair(subQueue);
    { Objects o(1,sub);o.invoke();assert(deletes==2 && repairsForTest()==1); }
    { Objects o(1,Nil,subQueue);o.invoke();assert(deletes==2 && repairsForTest()==1); }
    for(unsigned i=0;i<4000;++i){Objects o(1);o.invoke();}
    std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([]{
        for(unsigned i=0;i<128;++i){Objects o(1);o.invoke();}
    });
    for(auto &thread:threads)thread.join();
    assert(deletes==5026 && repairsForTest()==5025);assert(munmap(code,5314944)==0);
    std::puts("PASS: exact donor reproduces sole-buffer leak; native delete runs once after repair.");
    std::puts("PASS: shared/unknown ownership, subclasses, nulls, flags and shifted ObjC ABI stay guarded.");
    std::puts("PASS: 5025 repaired lifetimes including eight concurrent queues; no driver load or GPU submission.");
}
