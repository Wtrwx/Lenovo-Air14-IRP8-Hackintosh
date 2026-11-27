# EFI 下载与安装

| 适用项 | 本次基线 |
| --- | --- |
| 机型 | Lenovo 小新 Air14 IRP8 |
| 处理器与核显 | Core i5-1340P / Iris Xe `8086:A7A0` |
| 屏幕 | `E6F:1416`，2880 × 1800 |
| Wi-Fi / 蓝牙 | Intel AX211，`8086:51F1` / `8087:0033` |
| macOS | Tahoe 26.7.2（25G332） |
| OpenCore | 1.0.8 |

下载预编译 ZIP 和发布页上的 `SHA256SUMS.txt`。ZIP 内含 `EFI/BOOT`、`EFI/OC`、组件来源及许可证。[下载 EFI ZIP](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/download/v2026.11.27/Air14-IRP8-Tahoe-25G332-EFI.zip) · [发布与校验](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/tag/v2026.11.27)

1. 在下载目录运行 `shasum -a 256 -c SHA256SUMS.txt`，再解压 ZIP。
2. 在 `EFI/OC/config.plist → PlatformInfo → Generic` 填入自己原有的 `SystemSerialNumber`、`MLB`、`SystemUUID` 和 `ROM`。公开版将这些值留给使用者填写，机型为 `MacBookPro16,2`。使用 OpenCore 1.0.8 的 `ocvalidate` 校验编辑后的配置。[OpenCore iServices 指南](https://dortania.github.io/OpenCore-Post-Install/universal/iservices.html)
3. 要使用这台机器实测的 USB 端口映射、电池及内置蓝牙 USB 端口配置，在解压目录运行 `python3 prepare_efi_extras.py EFI`。脚本从上游下载并校验 `USBToolBox 1.2.0` 与 `ECEnabler 1.0.6`，启用它们和包内的 Air14 端口映射，并保留修改前的配置。[USBToolBox](https://github.com/USBToolBox/kext/releases/tag/1.2.0) · [ECEnabler](https://github.com/averycblack/ECEnabler/releases/tag/1.0.6)
4. 需要麦克风时，在解压目录完成下方的[音频准备](#启用麦克风)，然后用 OpenCore 1.0.8 的 `ocvalidate` 校验最终配置。
5. 备份现有 EFI，把准备好的 `EFI` 目录复制到 FAT32 EFI 分区。先由固件启动菜单选定引导设备，再测试 macOS 启动。发布配置的 `LauncherOption` 为 `Disabled`，适用于从单独的试装引导入口启动。
6. 图形加速需要单独安装 `WhateverXe Graphics v0.4.4`。该包的 `AppleIntelTGLCompat 0.8.1` 与 EFI 中的 `WhateverXe 0.8.3-air14` 配套。按上游说明安装并重启后，可按本仓库说明应用 H.264 硬编修正。[Graphics v0.4.4](https://github.com/b00t0x/WhateverXe-Graphics/releases/tag/v0.4.4) · [H.264 修正](setup.md#h264-硬件编码修补与自检)

公开包中的 HFS 驱动为 `OpenHfsPlus`，图形签名配套为 `AMFIAnchor 0.1.2` 和上游的 `AMFIAnchorInjector`。[AMFIAnchor](https://github.com/b00t0x/AMFIAnchor/tree/0.1.2)

测试隔空投送时将 Mac 连到 Wi-Fi，打开两端的 Wi-Fi、蓝牙和接收权限。先用小文件各发一次。状态记录中的双向成功来自上述机型的实机。公开 ZIP 通过 `ocvalidate` 和文件校验，需要按实际机器完成启动及外设验收。[实机记录](status-2026-11-26.md) · [Apple 隔空投送说明](https://support.apple.com/zh-cn/guide/mac-help/mh35868/mac)

## 启用麦克风

在 ZIP 解压目录运行以下命令。

```sh
python3 prepare_efi_extras.py EFI --audio
```

脚本从上游下载并校验实测版本 `SimpleHDA 1.0.0`，启用它与包内的 `IntelSSTDMIC`。加载顺序为 `Lilu → AppleALC → IntelSSTDMIC → SimpleHDA`。AppleALC 继续使用本机布局 `101`，Intel SST 使用 ADL SOF 固件和布局 `4`。SimpleHDA 的 AppleHDA 载荷由上游提供。[SimpleHDA 1.0.0](https://github.com/laobamac/SimpleHDA/releases/tag/v1.0.0) · [IntelSSTDMIC 构建说明](../sources/IntelSSTDMIC)

重启后先拔掉耳麦，在「系统设置 → 声音 → 输入」选择内置麦克风并录一段，随后插入 3.5 mm 耳麦测试外接输入。要回到音频准备前的配置，可将 `EFI/OC/config.before-audio.plist` 恢复为 `config.plist`。若只需停用数字麦克风，可在 `Kernel → Add` 禁用 `IntelSSTDMIC.kext`，保留 `SimpleHDA` 和 `AppleALC` 供 3.5 mm 耳麦使用。[实机记录](status-2026-11-27.md)
