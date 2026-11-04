# 来源与修改范围

显卡代码来源于 [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) 的 0.7.0 源码快照，原始许可与版权保留于 `sources/WhateverXe/LICENSE.txt`。本仓库不声称原创上游驱动，也没有上游 Git 历史；来源标注为版本快照，而非未经核实的 commit ID。

本地修改主要包括：

- 固件 VBT 的面板类型为 `0xff` 时，使用调用方提供的有效 EDID base block 做有界、唯一的 PNP 身份匹配。
- 显式 `-wex-edp-panel-only` 策略，限制为已经通过固件准入的内屏，禁用外部输出。
- 保留符合条件的 eDP 固件 handoff 状态，区分内屏与外部接口的控制字段。
- 接受 framebuffer 映射最后不足一整页的合法范围，并保留完整映射范围检查。
- 内屏中断恢复与热插拔专用中断路径的修正。
- 参照 Linux i915 的双像素时钟要求，修正 Display 13 watermark 的 donor 时钟单位换算，保留带宽、时序与实际读回保护。
- 相应的独立 CPU/mock 回归检查及英日文说明。

`docs/graphics-source-sha256.json` 记录当前公开显卡源码的文件校验值。构建产物、开发依赖、上游发布工作流、本机日志和 Apple 驱动载荷未纳入源码快照。

ACPI 中的 PNLF 和 polling 触控板 SSDT、I2C 匹配注入器、EDID 校验工具及本仓库文档是 Air14 配套适配内容，按仓库 BSD-3-Clause 许可提供。它们不包含整机固件表或面板原始转储。

当前时钟单位修正已完成 CPU 检查、构建及真实启动读回；这些检查不能替代持续稳定性验证。已知 GPU 挂起、界面卡顿与 WindowServer 内存增长保留为待解决问题。
