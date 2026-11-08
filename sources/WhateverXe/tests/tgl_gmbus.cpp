#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "kern_tgl_gmbus.hpp"
struct IO {
    unsigned time {},reads {},readyAt {},errorAt {};
    bool unavailable {};
    void delayUs(unsigned us) {assert(us==10);time+=us;}
    uint32_t readStatus() {++reads;return unavailable ? UINT32_MAX :
        errorAt && time>=errorAt ? 0x8600 : readyAt && time>=readyAt ? 0x8A00 : 0x8200;}
};
struct Request {
    uint32_t sendTransactionType {},replyTransactionType {},sendAddress {},replyAddress {};
    uint8_t sendSubAddress {},replySubAddress {};uint64_t minReplyDelay {};
    uint32_t commFlags {},sendBytes {},replyBytes {};uintptr_t sendBuffer {},replyBuffer {};
};
static void eligibility() {
    using namespace Display13GMBus;
    Request e;e.replyTransactionType=1;e.replyAddress=0xA0;e.replyBytes=128;
    auto w=qualify(true,true,0,&e);assert(w.read && !w.write);
    Request r;r.sendTransactionType=r.replyTransactionType=1;r.sendAddress=r.replyAddress=0xA8;
    r.sendBytes=r.replyBytes=1;r.sendBuffer=0x1000;r.replyBuffer=0x2000;
    w=qualify(true,true,0,&r);assert(w.read && w.write);
    for(unsigned n=0;n<16;++n) {
        auto bad=r;bool owner=true,selected=true;unsigned speed=0;
        switch(n) {
        case 0:owner=false;break;case 1:selected=false;break;case 2:speed=1;break;
        case 3:bad.sendAddress=0x74;break;case 4:bad.replyAddress=0x74;break;
        case 5:bad.sendTransactionType=2;break;case 6:bad.replyTransactionType=2;break;
        case 7:bad.sendBytes=2;break;case 8:bad.replyBytes=128;break;
        case 9:bad.sendBuffer=0;break;case 10:bad.replyBuffer=0;break;
        case 11:bad.commFlags=1;break;case 12:bad.minReplyDelay=1;break;
        case 13:bad.sendSubAddress=1;break;case 14:bad.replySubAddress=1;break;
        case 15:bad.sendBytes=0;break;
        }
        auto no=qualify(owner,selected,speed,&bad);assert(!no.read && !no.write);
    }
    auto no=qualify<Request>(true,true,0,nullptr);assert(!no.read && !no.write);
    Request write=r;write.sendBytes=2;write.replyTransactionType=write.replyBytes=0;write.replyBuffer=0;
    auto send=qualify(true,true,0,&write);assert(!send.read && send.write);
    assert(boundary(w,0xC5108,0x7E8,true) && boundary(w,0xC5108,0x45C,true));
    assert(boundary(send,0xC5108,0x45C,true) && !boundary(send,0xC5108,0x7E8,true));
    assert(!boundary(w,0xC510C,0x7E8,true) && !boundary(w,0xC5108,0x7E8,false));
    for(unsigned offset:{0x247U,0x270U,0x4D9U,0x790U,0x7B3U,0x90CU,0x9A5U})
        assert(!boundary(w,0xC5108,offset,true));
    // Recorded SCDC read: first and native wait reads both returned busy 0x8200.
    // Only the qualified wait boundary may add rereads until real HW_RDY.
    IO io;io.readyAt=140;uint32_t status=0x8200;
    assert(!boundary(w,0xC5108,0x7B3,true) && !io.reads);
    if(boundary(w,0xC5108,0x7E8,true))status=waitReady(io,status).value;
    assert(status==0x8A00 && io.time==140 && io.reads==14);
}
int main() {
    eligibility();
    assert(!Display13GMBus::waitSites(nullptr,0));
    uint8_t donor[0x800] {};
    const uint8_t send[] {0xBE,0x08,0x51,0x0C,0x00,0xE8,0x6C,0xA4,0x00,0x00,0x0F,0xBA,0xE0,0x0B,0x73,0x16};
    const uint8_t read[] {0xBE,0x08,0x51,0x0C,0x00,0xE8,0xE0,0xA0,0x00,0x00,0x0F,0xBA,0xE0,0x0B,0x73,0x50};
    memcpy(donor+0x452,send,sizeof(send));memcpy(donor+0x7DE,read,sizeof(read));
    assert(Display13GMBus::waitSites(donor,sizeof(donor)));
    assert(!Display13GMBus::waitSites(donor,0x7ED));
    for(unsigned off:{0x452U,0x457U,0x45CU,0x461U,0x7DEU,0x7E3U,0x7E8U,0x7EDU}) {
        donor[off]^=1;assert(!Display13GMBus::waitSites(donor,sizeof(donor)));donor[off]^=1;
    }
    {IO io;io.readyAt=140;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8A00 && r.elapsedUs==140 && r.reads==14 && io.reads==14);}
    {IO io;io.readyAt=50000;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8A00 && r.elapsedUs==50000 && r.reads==5000);}
    {IO io;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8200 && r.elapsedUs==50000 && r.reads==5000);}
    {IO io;io.errorAt=90;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==0x8600 && r.elapsedUs==90 && r.reads==9);}
    {IO io;io.unavailable=true;auto r=Display13GMBus::waitReady(io,0x8200);
     assert(r.value==UINT32_MAX && r.elapsedUs==10 && r.reads==1);}
    for(auto initial:{0x8800U,0x8600U,UINT32_MAX}) {
     IO io;auto r=Display13GMBus::waitReady(io,initial);assert(r.value==initial && !r.elapsedUs && !r.reads && !io.reads);}
    puts("PASS selected EDID/SCDC exact wait qualification and real GMBUS HW_RDY/NAK polling: delayed readiness, exact deadline, finite timeout, error/unavailable and already-ready fast paths.");
}
