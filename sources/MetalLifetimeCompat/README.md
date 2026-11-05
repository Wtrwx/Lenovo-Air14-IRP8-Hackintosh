# MetalLifetimeCompat

Experimental lifetime adapter for the exact Tiger Lake Metal donor on Darwin
25.6.0 and physical Intel 8086:A7A0. It allows the native command-buffer destructor
to reclaim an uncached target table that otherwise survives each sole command
buffer. The encoding-time sole-buffer flag and GPU submission behavior remain
intact. Shared or unknown ownership is left to the native driver.

The adapter checks driver identity, immutable code, the original companion and
runtime Objective-C ivar types and offsets. CPU replay reproduces the original
leak and checks native deletion, shifted superclass layouts and concurrent
lifetimes under AddressSanitizer and UndefinedBehaviorSanitizer. Hardware memory
growth, remote display and GPU stability are **not verified**.

Build with:

```sh
python3 build.py --donor /path/to/AppleIntelTGLGraphicsMTLDriver.bundle \
  --output /path/to/new-private-directory
```

The builder creates a locally signed private bundle candidate and checks its
original companion linkage. It does not install, reboot, load the Apple Metal
driver or submit GPU work. The matching WEX source in this repository admits
the exact companion path required by sandboxed WebKit GPU processes. Keep a
complete backup of the installed Metal bundle and the previous WEX for recovery.

Apple binaries and generated bundles remain private build inputs and outputs.
Only original MIT-licensed source is distributed here. Driver packaging and the
preserved compatibility companion come from
[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics).
