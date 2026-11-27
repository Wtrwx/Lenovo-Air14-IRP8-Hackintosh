# Lenovo Air14 IRP8 Hackintosh

联想小新 Air14 IRP8 的预编译 EFI、macOS 适配源码与配置工具。当前显卡基线使用 WhateverXe `0.8.3` 与 Graphics `v0.4.4`。[WhateverXe](https://github.com/b00t0x/WhateverXe) · [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)

## EFI 下载

**Air14 IRP8 · i5-1340P / AX211 · macOS Tahoe 26.7.2（25G332）**。发行包提供 OpenCore 引导目录、预编译内置麦克风驱动和 SHA‑256 校验文件。使用前请在配置中填入自己的设备标识，并按安装说明准备音频和图形依赖。[下载 EFI ZIP](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/download/v2026.11.27/Air14-IRP8-Tahoe-25G332-EFI.zip) · [GitHub 发布](https://github.com/Wtrwx/Lenovo-Air14-IRP8-Hackintosh/releases/tag/v2026.11.27) · [安装与适用范围](docs/efi-release.md)

## 硬件与验证范围

| 项目 | 本机配置 |
| --- | --- |
| 机型 | Lenovo 小新 Air14 IRP8 |
| CPU | Intel Core i5-1340P |
| GPU | Intel Iris Xe，PCI ID `8086:A7A0`（rev `0x04`） |
| 内存 | 16 GB |
| 内屏 | 2880 × 1800，面板标识 `E6F:1416` |
| 系统 | macOS Tahoe 26.7.2，Build 25G332 |

验证仅覆盖这台机器和上述系统；同名机型的其他面板、固件及硬件组合需要重新核对。

## 当前状态

| 功能 | 状态 |
| --- | --- |
| 内置盘 OpenCore 启动、默认 macOS | 已验证（含 ESP FAT32 清理/校验与修复记录） |
| 显卡核心 (`WhateverXe 0.8.3-air14` + `Graphics v0.4.4`) | 已验证：包含睡眠/掉电恢复、`eDP` 移交、Metal `#47` 内存增长显著抑制；长周期 GPU 队列挂起仍在追踪 |
| 背光调节 | 已验证，使用 `SSDT-AIR-PNLF` 与匹配的面板配置 |
| 内屏 120Hz & HiDPI | 已验证：`1440 × 900 HiDPI @ 120Hz`（`0.8.3` OpRegion Mailbox 5 原生识别面板 EDID） |
| 机身 HDMI 输出 & HiDPI | 已实测 `3840 × 2160`（UI `1920 × 1080 HiDPI @ 144Hz`）与内屏 `120Hz` 同屏工作；已修复显示模式过多导致的设置卡死与 HiDPI 碰撞 |
| H.264 硬件编码 / 解码 | **标准硬编/硬解已修复并实测通过**（清零 `MEDIA_SURFACE_STATE.DW2` 双重 pitch）；原生低延迟 `feature=0x400` 保持拦截防 VCS0 挂起 |
| Wi-Fi / 蓝牙 | AX211 使用 `ForbiddenItlwm 0.7.0-alpha.4`、`IntelBluetoothFirmware 2.5.3`、`BlueToolFixup 2.7.3` 与 `BTLEAdvFixup 0.9.8`；耳机与长睡眠回连待验 |
| 隔空投送 | 内置 EFI 启动后，macOS 日志记录发送和接收各成功一次 |
| 电池 | 电量与充电器拔插状态切换已验证（`ECEnabler` + `SMCBatteryManager`） |
| 触控板 | 移动、按压左键、轻点、双指滚动、右下角右键可用（`VoodooI2C` polling） |
| 开合盖与睡眠唤醒 | 集成 `WhateverXe 0.8.3` 唤醒内存与显示恢复机制并实测常规使用；长睡眠 S0ix / DarkWake 全场景仍在验证 |
| 摄像头 | 原生 UVC 实际预览、应用访问和 90 秒持续采集已验证；IR 红外与通话长期压测待补 |
| USB-C DP Alt Mode 视频输出 | 未解决；与普通 USB、Thunderbolt 分别验证 |
| 麦克风 | 内置 Intel SST 数字麦克风与 3.5 mm 耳麦均已实测录音。[音频配置](docs/efi-release.md#启用麦克风) · [实机记录](docs/status-2026-11-27.md) |
| 电源键 / Fn | 原生亮度键、Insert/Print Screen 已改用 EFI `SSDT-BKEY`；更多组合键与 `Fn+Q` 待验证 |
| CPU 温度 | Raptor Lake-P DTS 插件已验证有效读数与负载变化；风扇 RPM 接口未适配 |
| Thunderbolt | 普通 USB 扩展坞设备可枚举；NHI、热插拔和雷电设备未验证 |
| Apple 账户 | 系统设置登录与 iCloud 账户保存已验证；App Store 下载及 Siri 待验 |

本轮隔空投送和 EFI 发布记录，以及此前图形与外设进展。[2026-11-26 更新](docs/status-2026-11-26.md) · [2026-11-25 更新](docs/status-2026-11-25.md) · [已知问题](docs/known-issues.md)

## 仓库内容

- [sources/WhateverXe](sources/WhateverXe)：基于 WhateverXe `0.8.3` 的本机适配显卡驱动源码、`AppleIntelTGLCompat 0.8.1` 源码及 CPU/mock 测试套件（`227` 项通过）。
- [sources/SMCProcessor](sources/SMCProcessor)：保留 VirtualSMC 的 Raptor Lake-P 温度插件源码、构建方式和能力边界。
- [ACPI](ACPI)：背光（`SSDT-AIR-PNLF`）、触控板（`SSDT-AIR-I2CP`）及原生亮度键（`SSDT-BKEY`）的自定义 SSDT 源码。
- [config](config)：OpenCore 配置片段与 I2C 匹配注入器的 Info.plist。
- [IntelSSTDMIC 适配](sources/IntelSSTDMIC) 提供内置数字麦克风修复补丁与构建说明。
- [tools/patch_tgl_vadriver_h264.py](tools/patch_tgl_vadriver_h264.py)：针对 `WhateverXe-Graphics v0.4.4` 的 `AppleIntelTGLGraphicsVADriver.bundle` 修补脚本，支持保留低延迟 `0x400` 拦截的同时修复标准 H.264 硬件编码（`MEDIA_SURFACE_STATE.DW2`），或一键生成纯软件回退包。
- [tools/verify_vt_h264.mm](tools/verify_vt_h264.mm) / [tools/run_verify_vt_h264.py](tools/run_verify_vt_h264.py)：无外部依赖的 VideoToolbox H.264 编码画质（Y/U/V PSNR）、码率、Slice QP 解析与防挂起自检工具。
- [tools/patch_intelbtunified_ax211_fw.py](tools/patch_intelbtunified_ax211_fw.py)：将 `IntelBluetoothFirmware 2.5.3` 内较新的 `2026-01-28` / `2024-01-31` AX211 固件注入 `IntelBTUnified 1.0.1` 的可复现二进制修补脚本。
- [tools/fix_panel_edid.py](tools/fix_panel_edid.py)：为已核对的 `E6F:1416` 面板重算 DisplayID 校验，生成本地显示覆盖候选文件。
- [tools/make_hidpi_override.py](tools/make_hidpi_override.py)：生成零 LoDPI/HiDPI 碰撞的显示器 HiDPI `scale-resolutions` 候选文件，并在条目过多时提示 `MPDisplayMgr` 卡顿风险。
- [构建与配置说明](docs/setup.md)、[来源与修改范围](docs/provenance.md)。

预编译 EFI 通过 GitHub Releases 提供。源码目录提供公共配置模板，EFI 附件附有各组件的源码与许可说明。[公共配置模板](config/Air14-IRP8-Tahoe-25G332.plist) · [来源与修改范围](docs/provenance.md)

## 构建

需要 macOS、Xcode Command Line Tools、Python 3.9+、MacKernelSDK 和带开发资源的 Lilu。先阅读 [构建与配置说明](docs/setup.md)，在 `sources/WhateverXe` 中运行：

```sh
python3 tools/build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
python3 tools/test.py
```

CPU/mock 测试通过表示源码契约检查通过，不能替代完整真机硬件与长周期稳定性验证。

## 许可与致谢

使用 BSD-3-Clause 许可，见 [LICENSE.txt](LICENSE.txt)。WhateverXe 源码保留原项目的许可和版权，见其 [LICENSE.txt](sources/WhateverXe/LICENSE.txt)。第三方依赖各自遵循其原有许可。

感谢 [WhateverXe](https://github.com/b00t0x/WhateverXe)、[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)、[IntelBTUnified](https://github.com/laguillo/IntelBTUnified)、[WhateverGreen](https://github.com/acidanthera/WhateverGreen)、[Lilu](https://github.com/acidanthera/Lilu)、[OpenCorePkg](https://github.com/acidanthera/OpenCorePkg)、[VirtualSMC](https://github.com/acidanthera/VirtualSMC)、[VoodooI2C](https://github.com/VoodooI2C/VoodooI2C) 及其贡献者。显卡适配的其他参考来源保留在 WhateverXe 的 README 中。
