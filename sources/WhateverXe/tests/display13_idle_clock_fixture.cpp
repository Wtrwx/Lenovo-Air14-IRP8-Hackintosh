// CLOCK_IO
struct IOService {};
namespace Display13WM {
    struct Pipe {bool busy {};};
    static bool busyPipes(const Pipe (&p)[3]) {return p[0].busy || p[1].busy || p[2].busy;}
}
struct {
    IOService *controller {}; IO *map {}; bool clockStopped {true},clockFaulted {},watermarkFaulted {};
    struct {bool bootAdopted {true};struct {bool faulted {};} state;} bridge;
    Display13WM::Pipe pipe[3];
    struct {bool updating {},unretired {},faulted {};} dbuf;
} display13FBPower;
struct Display13FBClockIO {
    IO *map;
    bool readable(uint32_t r) const {return map->readable(r);}
    uint32_t read(uint32_t r) {return map->read(r);}
    void write(uint32_t r,uint32_t v) {map->write(r,v);}
    void delayUs(unsigned us) {map->delayUs(us);}
};
struct Display13FBClockGuard {};
static bool allowGuard=true,lastRestore=false;
static unsigned begins=0,ends=0,invalidations=0;
static bool adlnBeginClockGuard(Display13FBClockGuard &) {++begins;return allowGuard;}
static void adlnEndClockGuard(Display13FBClockGuard &,bool restore) {++ends;lastRestore=restore;}
static void adlnInvalidateWM() {++invalidations;}
// HELPER
static IOService controller,other;
static IO reset() {
    display13FBPower={};display13FBPower.controller=&controller;
    allowGuard=true;lastRestore=false;begins=ends=invalidations=0;
    IO io;io.regs[DSSM]=0x40000000;io.regs[PLL]=14;io.regs[Control]=0x780164;
    return io;
}
static bool restart(IO &io,Change &result,bool &guarded,uint32_t active=0,IOService *owner=&controller) {
    display13FBPower.map=&io;
    return adlnRestartIdleClock(owner,1305600000,active,read(io),result,guarded);
}
int main() {
    // Disabled 38.4 MHz-reference clock starts with no native gate needed.
    {auto io=reset();Change result;bool guarded=false;
     assert(restart(io,result,guarded));assert(guarded && begins==1 && ends==1 && lastRestore);
     assert(result.status==ChangeStatus::Complete && result.after.nativeHz()==1305600000);
     assert(io.writesTo(PLL)>0 && invalidations==1);}
    // Every exclusion must leave the original gated route responsible.
    for(unsigned scenario=0;scenario<12;++scenario) {
        auto io=reset();auto &c=display13FBPower;Change result;bool guarded=true;
        unsigned active=0;IOService *owner=&controller;
        switch(scenario) {
        case 0:active=1;break;case 1:active=6;break;
        case 2:c.clockStopped=false;break;
        case 3:io.regs[PLL]=0xC0000022;io.regs[Control]=0x380518;break;
        case 4:c.bridge.bootAdopted=false;break;case 5:c.bridge.state.faulted=true;break;
        case 6:c.clockFaulted=true;break;case 7:c.watermarkFaulted=true;break;
        case 8:c.pipe[2].busy=true;break;case 9:c.dbuf.updating=true;break;
        case 10:c.dbuf.unretired=true;break;case 11:owner=&other;break;
        }
        assert(!restart(io,result,guarded,active,owner));
        assert(!guarded && !begins && !ends && !invalidations && io.writes.empty());
    }
    {auto io=reset();display13FBPower.dbuf.faulted=true;Change result;bool guarded=false;
     assert(!restart(io,result,guarded) && io.writes.empty() && !begins);}
    // Failure to establish IRQ/consumer quiescence cannot write the clock.
    {auto io=reset();allowGuard=false;Change result;bool guarded=true;
     assert(restart(io,result,guarded) && !guarded && begins==1 && !ends);
     assert(result.status==ChangeStatus::ActiveDividerChange && io.writes.empty() && !invalidations);}
    // Firmware and readback failures stay failures; no synthesized success.
    for(unsigned fault=0;fault<4;++fault) {
        auto io=reset();Change result;bool guarded=false;
        if(fault==0)io.mailboxTimeout=true;
        if(fault==1)io.prepareDenied=true;
        if(fault==2)io.badControl=true;
        if(fault==3)io.regs[Mailbox]=0x80000000;
        assert(restart(io,result,guarded) && guarded && begins==1 && ends==1);
        assert(result.status!=ChangeStatus::Complete && !invalidations);
        assert(lastRestore==!bool(result.writes));
        if(fault<2)assert(!io.writesTo(PLL) && !io.writesTo(Control));
    }
    puts("PASS idle-clock adapter: stopped restart, active exclusions, IRQ refusal, firmware/readback faults. Synthetic IO only.");
}
