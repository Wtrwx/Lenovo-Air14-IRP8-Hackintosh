# Air14 内置数字麦克风驱动

此目录的补丁以 IntelSSTDMIC `1.0.3`（`d30794efa718c004fb046fa318bb7baab719d01d`）为基线。补丁添加 Air14 控制器 `8086:51CA`，限制 Lilu 回调只处理 AppleHDA，并利用输入引擎元数据区分内置麦克风和线路输入。[IntelSSTDMIC 1.0.3](https://github.com/qqaoshi888/IntelSSTDMIC/tree/1.0.3) · [实机记录](../../docs/status-2026-11-27.md)

在装有 Xcode Command Line Tools 的 macOS 上准备 MacKernelSDK 与 Lilu 1.7.2 DEBUG 的开发资源。将以下路径换成各自的本地路径。[MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) · [Lilu 1.7.2](https://github.com/acidanthera/Lilu/releases/tag/1.7.2)

```sh
git clone --branch 1.0.3 --single-branch https://github.com/qqaoshi888/IntelSSTDMIC.git
cd IntelSSTDMIC
git apply /path/to/air14-imic.patch
make -C kext SDK=/path/to/MacKernelSDK LILU=/path/to/Lilu.kext/Contents/Resources
codesign --verify --strict kext/build/IntelSSTDMIC.kext
```

产物位于 `kext/build/IntelSSTDMIC.kext`。上游源码中的 TGL、ADL 和 CML 固件字节分别对应 SOF 2.2 的 `intel-signed/sof-tgl.ri`、`intel-signed/sof-adl.ri` 与 `intel-signed/sof-cnl.ri`。驱动和固件许可分别见 [LICENSE.txt](LICENSE.txt) 和 [SOF-LICENCE.Intel](SOF-LICENCE.Intel)。[SOF 2.2 固件来源](https://github.com/thesofproject/sof-bin/tree/fcd5ae85ead7ecd67ff199335b61fac825843ca0/v2.2.x/sof-v2.2/intel-signed)
