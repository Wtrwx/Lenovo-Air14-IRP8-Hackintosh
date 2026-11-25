# Lenovo Air14 IRP8 Hackintosh

联想小新 Air14 IRP8 的 macOS 适配源码、辅助工具与配置片段，当前显卡基线基于 [WhateverXe](https://github.com/b00t0x/WhateverXe) `0.8.3` 与 [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics) `v0.4.4`。

**当前状态：核心日常能力（内屏 120Hz HiDPI、外接 HDMI 144Hz HiDPI、标准 H.264 硬编/硬解、蓝牙 `IntelBTUnified`、触控板、电池与亮度热键）已可用；但原生低延迟 H.264 编码仍故意保持屏蔽，且长睡眠 S0ix、部分热键及少数长周期 GPU 调度队列挂起仍在继续收敛。** 本仓库不提供可直接替换使用的完整 EFI。

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
| 蓝牙 | **已修复 `bluetoothd` 崩溃循环**：`IntelBTUnified 1.0.1` + 注入 `2026-01-28` AX211 固件，替代 `BlueToolFixup`；A2DP/睡醒稳定性继续观测 |
| 电池 | 电量与充电器拔插状态切换已验证（`ECEnabler` + `SMCBatteryManager`） |
| 触控板 | 移动、按压左键、轻点、双指滚动、右下角右键可用（`VoodooI2C` polling） |
| 开合盖与睡眠唤醒 | 集成 `WhateverXe 0.8.3` 唤醒内存与显示恢复机制并实测常规使用；长睡眠 S0ix / DarkWake 全场景仍在验证 |
| 摄像头 | 原生 UVC 实际预览、应用访问和 90 秒持续采集已验证；IR 红外与通话长期压测待补 |
| USB-C DP Alt Mode 视频输出 | 未解决；与普通 USB、Thunderbolt 分别验证 |
| 声卡 / 麦克风 | 未解决；内置 SST 数字麦克风需要单独适配 |
| 电源键 / Fn | 原生亮度键、Insert/Print Screen 已改用 EFI `SSDT-BKEY`；更多组合键与 `Fn+Q` 待验证 |
| CPU 温度 | Raptor Lake-P DTS 插件已验证有效读数与负载变化；风扇 RPM 接口未适配 |
| Thunderbolt | 普通 USB 扩展坞设备可枚举；NHI、热插拔和雷电设备未验证 |
| Apple 账户 | 登录后退出及 App Store 登录失败尚未解决 |

最新全量进展见 [2026-11-25 更新](docs/status-2026-11-25.md)，历史节点见 [2026-11-09 更新](docs/status-2026-11-09.md) 与 [2026-11-07 状态更新](docs/status-2026-11-07.md)，已知边界见 [已知问题](docs/known-issues.md)。

## 仓库内容

- [sources/WhateverXe](sources/WhateverXe)：基于 WhateverXe `0.8.3` 的本机适配显卡驱动源码、`AppleIntelTGLCompat 0.8.1` 源码及 CPU/mock 测试套件（`227` 项通过）。
- [sources/SMCProcessor](sources/SMCProcessor)：保留 VirtualSMC 的 Raptor Lake-P 温度插件源码、构建方式和能力边界。
- [ACPI](ACPI)：背光（`SSDT-AIR-PNLF`）、触控板（`SSDT-AIR-I2CP`）及原生亮度键（`SSDT-BKEY`）的自定义 SSDT 源码。
- [config](config)：OpenCore 配置片段与 I2C 匹配注入器的 Info.plist。
- [tools/patch_tgl_vadriver_h264.py](tools/patch_tgl_vadriver_h264.py)：针对 `WhateverXe-Graphics v0.4.4` 的 `AppleIntelTGLGraphicsVADriver.bundle` 修补脚本，支持保留低延迟 `0x400` 拦截的同时修复标准 H.264 硬件编码（`MEDIA_SURFACE_STATE.DW2`），或一键生成纯软件回退包。
- [tools/verify_vt_h264.mm](tools/verify_vt_h264.mm) / [tools/run_verify_vt_h264.py](tools/run_verify_vt_h264.py)：无外部依赖的 VideoToolbox H.264 编码画质（Y/U/V PSNR）、码率、Slice QP 解析与防挂起自检工具。
- [tools/patch_intelbtunified_ax211_fw.py](tools/patch_intelbtunified_ax211_fw.py)：将 `IntelBluetoothFirmware 2.5.3` 内较新的 `2026-01-28` / `2024-01-31` AX211 固件注入 `IntelBTUnified 1.0.1` 的可复现二进制修补脚本。
- [tools/fix_panel_edid.py](tools/fix_panel_edid.py)：为已核对的 `E6F:1416` 面板重算 DisplayID 校验，生成本地显示覆盖候选文件。
- [tools/make_hidpi_override.py](tools/make_hidpi_override.py)：生成零 LoDPI/HiDPI 碰撞的显示器 HiDPI `scale-resolutions` 候选文件，并在条目过多时提示 `MPDisplayMgr` 卡顿风险。
- [构建与配置说明](docs/setup.md)、[来源与修改范围](docs/provenance.md)。

完整 EFI、SMBIOS 身份、原始 EDID/ACPI 转储、账户数据、诊断日志以及 Apple / 第三方驱动二进制均未纳入本仓库。Apple 显卡驱动载荷与蓝牙二进制需按上游说明自行获取。

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
