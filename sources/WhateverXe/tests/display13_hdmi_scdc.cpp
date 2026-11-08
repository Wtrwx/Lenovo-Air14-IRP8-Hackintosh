#include "kern_display13_hdmi_scdc.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
struct IO {
    uint8_t config=0xa0;
    unsigned calls=0,fail=0,disconnect=0;
    bool wrongReadback=false,noVersion=false;
    bool connected(){return !disconnect || calls<disconnect;}
    std::vector<unsigned> writes;
    bool read(unsigned reg,uint8_t &v){++calls;if(calls==fail)return false;v=reg==1?(noVersion?0:1):config;if(wrongReadback && calls==5)v^=1;return true;}
    bool write(unsigned reg,uint8_t v){++calls;writes.push_back((reg<<8)|v);if(reg==0x20)config=v;return calls!=fail;}
};
int main(){
    auto hdmi=Display13HDMI::linkPlan(0x90030200,2,2,600000000,true,true);
    assert(hdmi.valid && hdmi.scdc && hdmi.maximumHz==600000000);
    auto dvi=Display13HDMI::linkPlan(0x91030200,1,2,600000000,true,true);
    assert(dvi.valid && !dvi.scdc && dvi.maximumHz==165000000);
    assert(!Display13HDMI::linkPlan(0x92030200,1,2,600000000,true,true).valid);
    assert(!Display13HDMI::linkPlan(0x90130200,1,4,600000000,true,true).valid);
    assert(!Display13HDMI::linkPlan(0x91030200,2,2,600000000,true,true).valid);
    for(uint64_t rate:{165000000ULL,340000000ULL,340000001ULL,583610000ULL,600000000ULL}) {
        IO io;auto r=Display13HDMI::negotiateSCDC(io,rate,600000000,true);
        assert(r.complete && io.config==(rate>340000000?0xa3:0xa0));
        assert(r.sourceFlags==(rate>340000000?17U:0U));
    }
    {IO io;io.config=0xa3;auto r=Display13HDMI::negotiateSCDC(io,241570000,600000000,true);
     assert(r.complete && io.config==0xa0 && !r.sourceFlags);}
    for(unsigned fail=1;fail<=5;++fail){IO io;io.fail=fail;
        auto r=Display13HDMI::negotiateSCDC(io,583610000,600000000,true);
        assert(!r.complete && !r.sourceFlags && io.config==0xa0);}
    for(unsigned cut=1;cut<=5;++cut){IO io;io.disconnect=cut;
        auto r=Display13HDMI::negotiateSCDC(io,583610000,600000000,true);
        assert(!r.complete && !r.sourceFlags);}
    {IO io;assert(!Display13HDMI::negotiateSCDC(io,583610000,600000000,false).complete && !io.calls);}
    {IO io;assert(Display13HDMI::negotiateSCDC(io,340000000,340000000,false).complete && !io.calls);}
    for(uint64_t rate:{0ULL,600000001ULL}){IO io;
        assert(!Display13HDMI::negotiateSCDC(io,rate,600000000,true).complete && !io.calls);}
    {IO io;assert(!Display13HDMI::negotiateSCDC(io,400000000,340000000,true).complete && !io.calls);}
    for(unsigned failure:{0U,6U,7U}){IO io;io.wrongReadback=true;io.fail=failure;
     auto r=Display13HDMI::negotiateSCDC(io,583610000,600000000,true);
     assert(!r.complete && !r.sourceFlags && r.restored==(failure==0));}
    {IO io;io.noVersion=true;assert(!Display13HDMI::negotiateSCDC(io,583610000,600000000,true).complete && io.writes.empty());}
    puts("PASS SCDC negotiation core: rate limits, ratio/scrambling pair, reserved bits, low-rate reset, failure restoration and disconnects. Synthetic I2C; native integration not yet tested.");
}
