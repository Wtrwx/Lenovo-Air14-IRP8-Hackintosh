// Exact TGL donor HW_RDY boundaries lack status rereads in their time-only
// waits. Qualify retained-controller, selected-DDC EDID/SCDC requests and the
// precise read/write wait sites. Poll real HW_RDY/NAK with a finite budget.
#ifndef KERN_TGL_GMBUS_HPP
#define KERN_TGL_GMBUS_HPP
#include "kern_xelp_donor.hpp"
namespace Display13GMBus {
using Poll = XELPDonor::Poll;
struct Wait {bool read {},write {};};
inline bool waitSites(const uint8_t *function,size_t size) {
    static const uint8_t send[] {0xBE,0x08,0x51,0x0C,0x00,0xE8,0x6C,0xA4,0x00,0x00,0x0F,0xBA,0xE0,0x0B,0x73,0x16};
    static const uint8_t read[] {0xBE,0x08,0x51,0x0C,0x00,0xE8,0xE0,0xA0,0x00,0x00,0x0F,0xBA,0xE0,0x0B,0x73,0x50};
    return function && size>=0x7DE + sizeof(read) &&
        !memcmp(function+0x452,send,sizeof(send)) && !memcmp(function+0x7DE,read,sizeof(read));
}
template<class Request> Wait qualify(bool owner,bool selected,uint32_t speed,const Request *r) {
    if(!owner || !selected || speed || !r)return {};
    if(r->sendTransactionType==0 && r->replyTransactionType==1 &&
       r->replyAddress==0xA0 && r->replyBytes==128)return {true,false};
    // SCDC register-pointer read and two-byte register write only. DDC/CI,
    // arbitrary clients, indexed transactions and other pins remain native.
    if(r->sendTransactionType!=1 || r->sendAddress!=0xA8 || !r->sendBuffer ||
       r->commFlags || r->minReplyDelay || r->sendSubAddress || r->replySubAddress)return {};
    if(r->sendBytes==1 && r->replyTransactionType==1 && r->replyAddress==0xA8 &&
       r->replyBytes==1 && r->replyBuffer)return {true,true};
    if(r->sendBytes==2 && !r->replyTransactionType && !r->replyBytes && !r->replyBuffer)
        return {false,true};
    return {};
}
inline bool boundary(Wait w,uint64_t reg,uintptr_t offset,bool access) {
    return access && reg==0xC5108 && ((w.read && offset==0x7E8) || (w.write && offset==0x45C));
}
template<class IO> Poll waitReady(IO &io,uint32_t initial) {
    return XELPDonor::waitEDIDReady(io,initial);
}
}
#endif
