#!/usr/bin/env python3
"""Execute the production idle-clock adapter with real clock core, mocked IRQ/MMIO.

This covers the stopped-clock entrance before the native workloop gate. Active
requests must retain the existing gate and watermark-certificate path. It does
not validate physical display output or native workloop scheduling.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text()
helper=re.search(r'static bool adlnRestartIdleClock\(.*?\n\}',s,re.S).group()
adapter=s[s.index('void XE::adlnSetDualClock('):s.index('void XE::adlnSetDualClock(')+15000]
assert adapter.index('ready && adlnRestartIdleClock(')<adapter.index('loop=c.workLoop;')
assert 'active,current,result,idleGuard)' in adapter
assert 'adlnWorkLoopTryClose)(loop)' in adapter
assert 'Display13WM::clockConsumersReady(c.pipe,active,current.nativeHz())' in adapter
assert 'adlnVerifyWMLatch(frames[p],current.nativeHz(),nativeHz)' in adapter
assert '} else adlnFault(c.clockFaulted,__LINE__);' in adapter
io=(r/'tests/display13_clock_set.cpp').read_text().split('int main() {')[0]
code=(r/'tests/display13_idle_clock_fixture.cpp').read_text().replace('// CLOCK_IO',io).replace('// HELPER',helper)
with tempfile.TemporaryDirectory(prefix='d13-idle-clock-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
