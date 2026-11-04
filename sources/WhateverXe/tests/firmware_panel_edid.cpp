#include "kern_vbt.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
using namespace XELP::VBT;
static void w16(std::vector<uint8_t>&b,size_t o,unsigned v){b[o]=v;b[o+1]=v>>8;}
static void fix(std::vector<uint8_t>&e){e[127]=0;uint8_t s=0;for(auto c:e)s+=c;e[127]=-s;}
int main(int argc,char **argv) {
    std::vector<uint8_t> b(16*66),e(128),ptrs(145);
    const uint8_t header[]={0,255,255,255,255,255,255,0};memcpy(e.data(),header,8);
    const uint8_t id[]={0x12,0x34,0x56,0x78,1,2,3,4,7,32};memcpy(e.data()+8,id,10);fix(e);
    for(unsigned i=0;i<16;++i)w16(b,66*i+36,0xffff);
    auto select=[&](const std::vector<uint8_t>&data,const std::vector<uint8_t>&edid) {
        return panelFromEDID(data.data(),data.size(),data.data(),data.size(),nullptr,0,edid.data(),edid.size());
    };
    assert(select(b,e)==16);
    memcpy(b.data()+3*66+56,id,10);assert(select(b,e)==3);
    auto noDate=b;noDate[3*66+64]=noDate[3*66+65]=0;assert(select(noDate,e)==3);
    auto wrongDate=b;wrongDate[3*66+64]^=1;assert(select(wrongDate,e)==16);
    auto duplicate=b;memcpy(duplicate.data()+4*66+56,id,10);assert(select(duplicate,e)==16);
    duplicate=noDate;memcpy(duplicate.data()+4*66+56,noDate.data()+3*66+56,10);assert(select(duplicate,e)==16);
    // A unique exact match outranks a date-free fallback, never an arbitrary
    // first match among several equally strong candidates.
    memcpy(duplicate.data()+5*66+56,id,10);assert(select(duplicate,e)==5);
    for(unsigned byte:{0U,8U,16U,127U}){auto bad=e;bad[byte]^=1;assert(select(b,bad)==16);}
    auto shortEDID=e;shortEDID.pop_back();assert(select(b,shortEDID)==16);
    auto longEDID=e;longEDID.push_back(0);assert(select(b,longEDID)==16);
    for(unsigned byte:{8U,10U,12U}){auto bad=e;bad[byte]^=1;fix(bad);assert(select(b,bad)==16);}
    auto badTable=b;badTable[36]=0;assert(select(badTable,e)==16);
    for(unsigned length:{0U,37U,65U,1055U}){auto truncated=b;truncated.resize(length);assert(select(truncated,e)==16);}
    // Explicit pointer tables must stay inside BDB42 and contain ten-byte IDs.
    ptrs[0]=3;for(unsigned i=0;i<16;++i){w16(ptrs,1+9*i+6,66*i+56);ptrs[1+9*i+8]=10;}
    auto withPointers=[&](const std::vector<uint8_t>&p) {
        return panelFromEDID(b.data(),b.size(),b.data(),b.size(),p.data(),p.size(),e.data(),e.size());
    };
    assert(withPointers(ptrs)==3);
    for(unsigned bad=0;bad<4;++bad){auto p=ptrs;
        if(bad==0)p.resize(144);if(bad==1)p[0]=2;
        if(bad==2)w16(p,7,0xffff);if(bad==3)p[9]=9;
        assert(withPointers(p)==16);
    }
    // Optional private fixture replay: no machine identifiers are embedded
    // in this public synthetic test.
    if(argc==3) {
        std::ifstream vf(argv[1],std::ios::binary),ef(argv[2],std::ios::binary);assert(vf&&ef);
        std::vector<uint8_t> v{std::istreambuf_iterator<char>(vf),{}},ed{std::istreambuf_iterator<char>(ef),{}};
        auto before=parse(v.data(),v.size(),XELP::Family::RaptorLakeP);
        auto after=parse(v.data(),v.size(),XELP::Family::RaptorLakeP,ed.data(),ed.size());
        assert(before.valid()&&after.valid()&&!before.panel.valid&&after.panel.valid);
        printf("Private firmware replay: panel index=%u DDI=%u; connector mask=%x unchanged=%u\n",
            after.panel.type,after.panel.ddi,after.present,before.present==after.present);
    }
    puts("PASS EDID panel lookup: identity/date priority, ambiguity, malformed EDID, bounds and pointer records");
}
