# NativeAvcCompat

Experimental, source-built compatibility adapter for the native progressive H.264
VDEnc path in the Tiger Lake VA donor on Darwin 25.6.0 and physical Intel
8086:A7A0. It projects the extended SPS layout into the layout consumed by that
path. Driver identity, executable payload, imported companion, frame dimensions
and slice bounds must match; unsupported inputs return an error before submission.
Only progressive frames within the native command's size and slice-address limits are admitted.

CPU tests reproduce the invalid doubled slice coordinates in the exact donor and
verify the projection, caller memory isolation and completion outputs. Hardware
encoding, remote display and long-term GPU stability are **not verified**. This
adapter does not repair unrelated render hangs or GPU reset recovery.
The deployed candidate still failed real remote use. Bounded status messages
distinguish a mapped module from accepted runtime guards and an actual SPS projection.

Build with:

```sh
python3 build.py --donor /path/to/AppleIntelTGLGraphicsVADriver.bundle \
  --output /path/to/new-private-directory
```

The builder runs sanitizer tests and a
CPU-only native replay, then creates a locally signed bundle candidate and checks
its companion linkage. It does not install, reboot, change EFI or load the VA
driver. Keep an exact backup of the complete installed VA bundle for restoration.
Only use the candidate as part of a controlled hardware validation with that backup.

Apple binaries and generated bundles are private build inputs and outputs; this
directory distributes only original source under the MIT license.

This adapter builds on the driver packaging and companion compatibility work in
[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics).
