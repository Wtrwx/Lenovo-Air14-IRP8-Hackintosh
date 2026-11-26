# 来源与修改范围

从 `v2026.11.26` 起，预编译 EFI 作为 GitHub Release 附件提供。附件中的 `THIRD-PARTY.md` 列出各组件的版本与对应源码，`licenses/` 收录上游许可和声明。公共配置使用空白设备标识，显卡运行时和内置音频所需的 Apple 载荷由各自上游按需安装。[EFI 发布说明](efi-release.md)

显卡代码当前基于 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 的 `0.8.3` 源码快照（tag `0.8.3`，commit `f26a36880c49ea1063393d943527808c0f38feca`），搭配上游 [b00t0x/WhateverXe-Graphics](https://github.com/b00t0x/WhateverXe-Graphics) `v0.4.4`（commit `61c7cddda38d6a5d0057a01f402aab6efb612e13`，含 `AppleIntelTGLCompat 0.8.1`、`#47` Metal 内存修复、`#60` / `#96` VA 修复）；原始许可与版权保留于 `sources/WhateverXe/LICENSE.txt`。本仓库不声称原创上游驱动，也没有包含上游完整 Git 历史。

在 WhateverXe `0.8.3` 基础上，本机选择性保留与集成的修改主要包括：

- 显式 `-wex-edp-hdmi-only` 策略保留通过固件准入的内屏和固定 HDMI；原 `-wex-edp-panel-only` 保留为恢复策略（内部面板 EDID 识别则由 `0.8.3` 原生 OpRegion Mailbox 5 承接）。
- HDMI 根据 EDID 能力调整源端时钟上限，通过 SCDC 协商 HDMI 2.0；为标准 SCDC 请求补充有界的真实 GMBUS 就绪轮询，修正 SDR 深度和颜色数据包配置。寄存器定义参考 Linux i915。
- 保留符合条件的 eDP 固件 `CTL2 bit3` handoff 状态，区分内屏与外部接口的控制字段。
- 接受 framebuffer 映射最后不足一整页且满足 64 B 对齐的合法范围，并保留完整映射范围检查。
- 结合上游 `0.8.3` 的掉电/唤醒恢复（`restoreAfterPowerLoss`、`deferRearm`、`finishDisplay13SystemWake`、`restoreGGTT`、`SystemSleepEvents`、`TGLRecovery`）补充停用游标、退场路径、`VBL path->fb` 校验与多输出电源释放保护。
- 参照 Linux i915 的双像素时钟要求，修正 Display 13 watermark 的时钟单位换算，保留带宽、时序与实际读回保护，以适配内屏 120Hz 与外屏 144Hz。
- 默认暂不合入/不开启实验性 `-wex-video-progressive-fix` 路径，以保持主基线可隔离验收；相应的独立 CPU/mock 回归检查均已同步至 `227` 项通过。

`docs/graphics-source-sha256.json` 记录当前公开显卡源码的 474 个文件校验值。构建产物、开发依赖、上游发布工作流、本机日志和 Apple 驱动载荷未纳入源码快照。

`tools/` 中新增的可复现用户态工具按仓库 BSD-3-Clause 许可提供：

- `patch_tgl_vadriver_h264.py`：仅通过偏移验证和二进制补丁脚本输出针对 stock `WhateverXe-Graphics v0.4.4` 的 H.264 修复或软件回退包，仓库内不包含任何 Apple 二进制载荷。
- `verify_vt_h264.mm` 与 `run_verify_vt_h264.py`：基于 macOS 公开 `VideoToolbox` / `CoreVideo` / `ImageIO` 框架的独立画质、码率与 H.264 Annex-B Slice QP 校验工具。
- `patch_intelbtunified_ax211_fw.py`：依据 [laguillo/IntelBTUnified](https://github.com/laguillo/IntelBTUnified) (`v1.0.1`, GPL-3.0-only) 的 `_fwList` 结构，由用户本地的 `IntelBluetoothFirmware 2.5.3` 提取 AX211 较新固件，替换对应槽位。当前 EFI 使用上游 `IntelBluetoothFirmware 2.5.3`，修补后的 `IntelBTUnified` 由使用者本地生成。
- `prepare_efi_extras.py`：由用户下载并校验 `USBToolBox` 和 `ECEnabler`，启用 USB 端口映射与电池支持所需的配置。
- `make_hidpi_override.py` 与 `fix_panel_edid.py`：本地生成显示覆盖与校验修正 plist 的辅助脚本，包含 HiDPI/LoDPI 碰撞拒绝与数量预警。

ACPI 中的 PNLF、polling 触控板和亮度热键 SSDT、I2C 匹配注入器及文档同样按仓库 BSD-3-Clause 许可提供，不包含整机固件表、SMBIOS 身份或面板原始转储。

`sources/SMCProcessor` 基于 Acidanthera VirtualSMC 1.3.8 的 SMCProcessor 和公开 SDK，保留原许可、版权及上游文件校验值，仅启用 Intel family 6 model BA 的 DTS 温度路径。
