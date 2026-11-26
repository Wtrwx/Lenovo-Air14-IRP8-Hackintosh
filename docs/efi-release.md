# EFI 下载与安装

| 适用项 | 本次基线 |
| --- | --- |
| 机型 | Lenovo 小新 Air14 IRP8 |
| 处理器与核显 | Core i5-1340P / Iris Xe `8086:A7A0` |
| 屏幕 | `E6F:1416`，2880 × 1800 |
| Wi-Fi / 蓝牙 | Intel AX211，`8086:51F1` / `8087:0033` |
| macOS | Tahoe 26.7.2（25G332） |
| OpenCore | 1.0.8 |

下载预编译 ZIP 和发布页上的 `SHA256SUMS.txt`。ZIP 内含 `EFI/BOOT`、`EFI/OC`、组件来源及许可证。[下载 EFI ZIP](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/download/v2026.11.26/Air14-IRP8-Tahoe-25G332-EFI.zip) · [发布与校验](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/tag/v2026.11.26)

1. 在下载目录运行 `shasum -a 256 -c SHA256SUMS.txt`，再解压 ZIP。
2. 在 `EFI/OC/config.plist → PlatformInfo → Generic` 填入自己原有的 `SystemSerialNumber`、`MLB`、`SystemUUID` 和 `ROM`。公开版将这些值留给使用者填写，机型为 `MacBookPro16,2`。使用 OpenCore 1.0.8 的 `ocvalidate` 校验编辑后的配置。[OpenCore iServices 指南](https://dortania.github.io/OpenCore-Post-Install/universal/iservices.html)
3. 要使用这台机器实测的 USB 端口映射、电池及内置蓝牙 USB 端口配置，在解压目录运行 `python3 prepare_efi_extras.py EFI`。脚本从上游下载并校验 `USBToolBox 1.2.0` 与 `ECEnabler 1.0.6`，启用它们和包内的 Air14 端口映射，并保留修改前的配置。[USBToolBox](https://github.com/USBToolBox/kext/releases/tag/1.2.0) · [ECEnabler](https://github.com/averycblack/ECEnabler/releases/tag/1.0.6)
4. 备份现有 EFI，把准备好的 `EFI` 目录复制到 FAT32 EFI 分区。先由固件启动菜单选定引导设备，再测试 macOS 启动。发布配置的 `LauncherOption` 为 `Disabled`，适用于从单独的试装引导入口启动。
5. 图形加速需要单独安装 `WhateverXe Graphics v0.4.4`。该包的 `AppleIntelTGLCompat 0.8.1` 与 EFI 中的 `WhateverXe 0.8.3-air14` 配套。按上游说明安装并重启后，可按本仓库说明应用 H.264 硬编修正。[Graphics v0.4.4](https://github.com/b00t0x/WhateverXe-Graphics/releases/tag/v0.4.4) · [H.264 修正](setup.md#h264-硬件编码修补与自检)

公开包中的 HFS 驱动为 `OpenHfsPlus`，图形签名配套为 `AMFIAnchor 0.1.2` 和上游的 `AMFIAnchorInjector`。如需独立验证内置声卡，按 SimpleHDA 原项目说明添加其组件。[AMFIAnchor](https://github.com/b00t0x/AMFIAnchor/tree/0.1.2) · [SimpleHDA](https://github.com/laobamac/SimpleHDA/releases)

测试隔空投送时将 Mac 连到 Wi-Fi，打开两端的 Wi-Fi、蓝牙和接收权限。先用小文件各发一次。状态记录中的双向成功来自上述机型的实机。公开 ZIP 通过 `ocvalidate` 和文件校验，需要按实际机器完成启动及外设验收。[实机记录](status-2026-11-26.md) · [Apple 隔空投送说明](https://support.apple.com/zh-cn/guide/mac-help/mh35868/mac)
