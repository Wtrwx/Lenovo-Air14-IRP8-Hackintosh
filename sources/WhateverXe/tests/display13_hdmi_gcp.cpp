#include "kern_display13_hdmi_caps.hpp"
#include <map>
#include <vector>
#include <cassert>
#include <cstdio>
struct IO {
    std::map<unsigned,unsigned> regs;
    std::vector<std::pair<unsigned,unsigned>> writes;
    bool badReadback=false;
    unsigned read(unsigned r) {return regs.count(r)?regs[r]:UINT32_MAX;}
    void write(unsigned r,unsigned v) {writes.emplace_back(r,v);if(!badReadback)regs[r]=v;}
};
int main() {
    for(unsigned pipe=0;pipe<3;++pipe)for(unsigned code:{0U,1U,3U}) {
        const unsigned b=0x1000*pipe;
        IO io{{{0x60400+b,0x80030200|(code<<20)},{0x60200+b,0x1000},{0x60210+b,0x2a0f}}};
        auto r=Display13HDMI::ensureGCP(io,pipe);
        assert(r.eligible && r.complete && r.writes==2);
        assert(io.regs[0x60200+b]==0x11000 && io.regs[0x60210+b]==(code?4U:0U));
        assert(io.writes[0].first==0x60210+b && io.writes[1].first==0x60200+b);
    }
    for(unsigned f:{0U,0x80000000U|(2U<<24),0x80000000U|(2U<<20),UINT32_MAX}) {
        IO io{{{0x60400,f},{0x60200,0x1000}}};auto r=Display13HDMI::ensureGCP(io,0);
        assert(!r.eligible && io.writes.empty());
    }
    {IO io{{{0x60400,0x80100000},{0x60200,0}}};assert(!Display13HDMI::ensureGCP(io,0).eligible && io.writes.empty());}
    {IO io;assert(!Display13HDMI::ensureGCP(io,3).eligible && io.writes.empty());}
    {IO io{{{0x60400,0x80100000},{0x60200,0x1000},{0x60210,0x2a0f}}};io.badReadback=true;
     auto r=Display13HDMI::ensureGCP(io,0);assert(r.eligible && !r.complete && r.writes==1 && io.regs[0x60200]==0x1000);}
    puts("PASS HDMI GCP: 8/10/12bpc packets, per-transcoder banks, DP/offline exclusion and readback refusal. Synthetic IO only.");
}
