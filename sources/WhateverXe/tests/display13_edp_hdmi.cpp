#include "kern_display13_fb_topology.hpp"
#include <cassert>
#include <cstdio>

int main() {
    for(unsigned panel=0;panel<2;++panel)for(unsigned tc=0;tc<16;++tc) {
        const unsigned hdmi=1-panel;
        Display13OutputWiring board;board.valid=true;board.panel=1U<<panel;
        board.hdmi=1U<<hdmi;board.ddc[hdmi]=2;board.dp=board.typeC=tc<<3;
        assert(board.described());
        const auto w=board.panelAndHDMIOnly();
        assert(w.described() && w.selected()==3 && w.panel==board.panel && w.hdmi==board.hdmi);
        assert(!w.dp && !w.typeC && !w.dsi && w.ddc[hdmi]==2);
        for(unsigned d=0;d<Display13OutputWiring::Ports;++d)if(d!=hdmi)assert(!w.ddc[d]);
        assert(board.dp==(tc<<3) && board.typeC==board.dp);
        assert(w.transport(panel)==Display13OutputWiring::Transport::EDP);
        assert(w.transport(hdmi)==Display13OutputWiring::Transport::TMDS);
        assert(Display13FBTopology::supportedWiring(w,true));
        assert(Display13FBTopology::multiOutputWiring(w));
        assert(Display13FBTopology::deferredBootPrepare(w,false));
        assert(w.panelOnly().selected()==board.panel);
        alignas(8) uint8_t src[0x1c0]{},dst[0x1c0]{};src[0x12]=9;
        for(unsigned d=0;d<9;++d) {
            const uint32_t c[]={d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};
            memcpy(src+0x20+24*d,c,sizeof(c));
        }
        const uint32_t donorPanel[]={0,0,0,0,2,0x18};memcpy(src+0x20,donorPanel,24);
        assert(Display13FBTopology::apply(dst,sizeof(dst),src,w));
        assert(Display13FBTopology::boundPorts(dst,w).selected==3);
        for(unsigned d=0;d<9;++d) {
            const auto c=reinterpret_cast<const uint32_t *>(dst+0x20+24*d);
            if(d<2)assert(w.matches(c) && Display13FBTopology::selected(dst,c,w));
            else assert(c[4]==1 && !c[5] && !Display13FBTopology::selected(dst,c,w));
        }
        assert(Display13FBTopology::excludedHotplug(dst,sizeof(dst),w)==0x3f040000);
        assert(Display13FBTopology::bootDDI(w,0x82000000|((panel+1)<<27),true)==panel);
        assert(Display13FBTopology::bootDDI(w,0x80000000|((hdmi+1)<<27))==hdmi);
        assert(Display13FBTopology::bootDDI(w,0x9a000000,true)==UINT32_MAX);
        // Reject malformed firmware even when the offending route would be removed.
        auto bad=board;bad.valid=false;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.ddc[8]=9;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.dp|=4;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.ddc[hdmi]=0;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.panel=3;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.panel=0;assert(!bad.panelAndHDMIOnly().described());
        bad=board;bad.hdmi=0;bad.ddc[hdmi]=0;assert(!bad.panelAndHDMIOnly().described());
    }
    Display13OutputWiring tc;tc.valid=true;tc.panel=1;tc.hdmi=10;tc.ddc[1]=2;tc.ddc[3]=9;
    assert(tc.described());const auto restricted=tc.panelAndHDMIOnly();
    assert(restricted.described() && restricted.hdmi==2 && !restricted.ddc[3]);
    tc.hdmi=8;tc.ddc[1]=0;assert(tc.described() && !tc.panelAndHDMIOnly().described());
    tc.panel=0;tc.dsi=1;assert(tc.described() && !tc.panelAndHDMIOnly().described());
    puts("PASS eDP/Combo HDMI qualification, DDC retention, topology, hotplug exclusion and invalid-route rejection");
}
