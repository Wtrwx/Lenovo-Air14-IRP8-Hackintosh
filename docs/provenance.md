# 来源与修改范围

显卡代码来源于 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 的 0.7.0 源码快照，原始许可与版权保留于 `sources/WhateverXe/LICENSE.txt`。本仓库不声称原创上游驱动，也没有上游 Git 历史；来源标注为版本快照，而非未经核实的 commit ID。

本地修改主要包括：

- 固件 VBT 的面板类型为 `0xff` 时，使用调用方提供的有效 EDID base block 做有界、唯一的 PNP 身份匹配。
- 显式 `-wex-edp-hdmi-only` 策略保留通过固件准入的内屏和固定 HDMI；原 `-wex-edp-panel-only` 保留为恢复策略。
- HDMI 根据 EDID 能力调整源端时钟上限，通过 SCDC 协商 HDMI 2.0；为标准 SCDC 请求补充有界的真实 GMBUS 就绪轮询，修正 SDR 深度和颜色数据包配置。寄存器定义参考 Linux i915。
- 保留符合条件的 eDP 固件 handoff 状态，区分内屏与外部接口的控制字段。
- 接受 framebuffer 映射最后不足一整页的合法范围，并保留完整映射范围检查。
- 内屏中断恢复与热插拔专用中断路径的修正。
- 参照 Linux i915 的双像素时钟要求，修正 Display 13 watermark 的 donor 时钟单位换算，保留带宽、时序与实际读回保护。
- 无当前显示路径的 watermark setup/update 和 DBUF 分配回调隔离，保留有效路径的资源失败保护。
- 相应的独立 CPU/mock 回归检查及英日文说明。

`docs/graphics-source-sha256.json` 记录当前公开显卡源码的文件校验值。构建产物、开发依赖、上游发布工作流、本机日志和 Apple 驱动载荷未纳入源码快照。

ACPI 中的 PNLF、polling 触控板和亮度热键 SSDT、I2C 匹配注入器、EDID 校验工具及本仓库文档是 Air14 配套适配内容，按仓库 BSD-3-Clause 许可提供。它们不包含整机固件表或面板原始转储。

当前时钟单位修正已完成 CPU 检查、构建及真实启动读回；这些检查不能替代持续稳定性验证。已知 GPU 挂起、界面卡顿与 WindowServer 内存增长保留为待解决问题。

`sources/SMCProcessor` 基于 Acidanthera VirtualSMC 1.3.8 的 SMCProcessor 和公开 SDK，保留原许可、版权及上游文件校验值。新增仅针对物理 Intel family 6 model BA、同时具备核心与封装 DTS 能力的温度路径；不修改 Lilu 的共享 CPU 身份，也不构建或替换 VirtualSMC。该型号不启用未经验证的 RAPL、Voltage 或风扇接口。CPU 准入检查、Release 构建、温度负载变化和再次重启后的读数已验证；原始诊断继续保留在私人工作区。

原生亮度键的 PS2K 通知和 RMCF 映射依据 [Acidanthera VoodooPS2](https://github.com/acidanthera/VoodooPS2) 的键盘通知处理及 ACPI 配置机制。SSDT 保留其他系统原有 EC 查询方法，通知方向按本机用户反馈修正，尚未完成修正后的重启验证。
