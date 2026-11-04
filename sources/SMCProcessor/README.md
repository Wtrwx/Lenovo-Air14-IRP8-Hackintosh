# SMCProcessor for mobile Raptor Lake

This standalone plugin extends [VirtualSMC's SMCProcessor](https://github.com/acidanthera/VirtualSMC/tree/1.3.8/Sensors/SMCProcessor) 1.3.8 with temperature monitoring for Intel family 6 model `BA` (Raptor Lake-P) when Lilu reports an unknown CPU generation. It keeps the physical vendor/model and CPUID thermal-capability checks. Other unknown CPUs remain unsupported.

The added path exposes core and CPU-package DTS temperatures using read-only Intel thermal MSRs. It does not enable RAPL power, voltage or fan sensors on this model. Core labels follow the topology supplied by Lilu; complete, distinct P-core/E-core coverage is not claimed when another plugin rebuilds that topology. Use package temperature for whole-CPU monitoring.

Keep the existing VirtualSMC and Lilu installed. Enable exactly one SMCProcessor plugin after VirtualSMC in OpenCore; the generated bundle is `SMCProcessor-RaptorP.kext`, with executable `Contents/MacOS/SMCProcessor`. This source does not build or replace VirtualSMC. See the [project status](../../README.md) for hardware validation and limitations.

Build on macOS with Xcode Command Line Tools, Python 3.9+, MacKernelSDK and Lilu development resources:

```sh
python3 build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
clang++ -std=c++17 tests/thermal_admission.cpp -o /tmp/smcprocessor-admission
/tmp/smcprocessor-admission
```

The CPU test checks admission boundaries; hardware temperature response and reboot persistence require separate validation. Standard `sp78` values are signed big-endian fixed point with eight fractional bits (for example, bytes `55 00` mean 85°C).

Upstream source provenance is recorded in [upstream.json](upstream.json). The original BSD-3-Clause license and copyrights are retained in [LICENSE.txt](LICENSE.txt) and the source files.
