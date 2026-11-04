# Lenovo Air14 IRP8 Hackintosh

联想小新 Air14 IRP8 的 macOS 适配源码与配置片段，基于 [WhateverXe](https://github.com/b00t0x/WhateverXe) 0.7.0。

**当前为实验性适配。内屏 Metal、背光、HiDPI 和 120Hz 已实际工作，但仍发生过 GPU 渲染队列挂起及整机界面无响应，稳定性问题尚未解决。** 本仓库不提供可直接替换使用的完整 EFI。

## 硬件与验证范围

| 项目 | 本机配置 |
| --- | --- |
| 机型 | Lenovo 小新 Air14 IRP8 |
| CPU | Intel Core i5-1340P |
| GPU | Intel Iris Xe，PCI ID `8086:A7A0` |
| 内存 | 16 GB |
| 内屏 | 2880 × 1800，面板标识 `E6F:1416` |
| 系统 | macOS Tahoe 26.7.2，Build 25G332 |

验证仅覆盖这台机器和上述系统；同名机型的其他面板、固件及硬件组合需要重新核对。

## 当前状态

| 功能 | 状态 |
| --- | --- |
| 内置盘 OpenCore 启动、默认 macOS | 已验证 |
| 内屏 Metal | 已通过计算与读回检查；仍有卡顿和 GPU 挂起 |
| 背光调节 | 已验证，使用 PNLF 与匹配的面板配置 |
| 120Hz | 模式枚举与实际刷新率均已验证 |
| HiDPI | 1440 × 900 HiDPI 已验证 |
| 电池 | 电量与充电器拔插状态切换已验证 |
| 触控板 | 移动、按压左键、轻点、双指滚动、右下角右键可用 |
| 开合盖 | 日常操作已确认可用；未完成所有睡眠唤醒场景验证 |
| 摄像头 | 原生 UVC 实际预览、应用访问和 90 秒持续采集已验证；视频通话和编码未验证 |
| 机身 HDMI 输出 | 未解决；当前策略禁用外部显示输出 |
| USB-C DP Alt Mode 视频输出 | 未解决；与普通 USB、Thunderbolt 分别验证 |
| 声卡 / 麦克风 | 未解决；内置 SST 数字麦克风需要单独适配 |
| 蓝牙 | 当前停用，待单项验证 |
| 电源键 / Fn | 电源键响应待修复，Fn 暂缓 |
| CPU 温度 | Raptor Lake-P DTS 插件已验证有效读数、负载变化及再次重启后的读取；P/E 核逐核覆盖未完整验证 |
| 风扇转速 | 尚未找到并验证本机 RPM 接口 |
| Thunderbolt | 普通 USB 扩展坞设备可枚举；NHI、热插拔和雷电设备未验证 |
| Apple 账户 | 登录后退出及 App Store 登录失败尚未解决 |

详细限制见 [已知问题](docs/known-issues.md)。

## 仓库内容

- [sources/WhateverXe](sources/WhateverXe)：包含本机适配修改的显卡插件、Compat 源码及原有 CPU 测试。
- [sources/SMCProcessor](sources/SMCProcessor)：保留 VirtualSMC 的 Raptor Lake-P 温度插件源码、构建方式和能力边界。
- [ACPI](ACPI)：背光及触控板的自定义 SSDT 源码。
- [config](config)：OpenCore 配置片段与 I2C 匹配注入器的 Info.plist。
- [tools/fix_panel_edid.py](tools/fix_panel_edid.py)：为已核对的同型号面板修正 DisplayID 校验，生成本地显示覆盖候选文件。
- [构建与配置说明](docs/setup.md)、[来源与修改范围](docs/provenance.md)。

完整 EFI、SMBIOS 身份、原始 EDID/ACPI 转储、账户数据和诊断日志没有包含在仓库中。Apple 显卡驱动载荷需按上游 [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics) 的说明单独获取。

## 构建

需要 macOS、Xcode Command Line Tools、Python 3.9+、MacKernelSDK 和带开发资源的 Lilu。先阅读 [构建与配置说明](docs/setup.md)，在 `sources/WhateverXe` 中运行：

```sh
python3 tools/build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
python3 tools/test.py
```

CPU/mock 测试通过仅表示源码契约检查通过，不代表本机显卡已稳定，也不能替代真实启动验证。

## 许可与致谢

使用 BSD-3-Clause 许可，见 [LICENSE.txt](LICENSE.txt)。WhateverXe 源码保留原项目的许可和版权，见其 [LICENSE.txt](sources/WhateverXe/LICENSE.txt)。第三方依赖各自遵循其原有许可。

感谢 [WhateverXe](https://github.com/b00t0x/WhateverXe)、[WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics)、[WhateverGreen](https://github.com/acidanthera/WhateverGreen)、[Lilu](https://github.com/acidanthera/Lilu)、[OpenCorePkg](https://github.com/acidanthera/OpenCorePkg)、[VirtualSMC](https://github.com/acidanthera/VirtualSMC)、[VoodooI2C](https://github.com/VoodooI2C/VoodooI2C) 及其贡献者。显卡适配的其他参考来源保留在 WhateverXe 的 README 中。
