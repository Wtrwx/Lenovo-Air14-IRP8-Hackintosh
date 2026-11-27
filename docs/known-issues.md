# 已知问题

## GPU 与视频编解码

- **标准 H.264 硬件编码（已修复）**：经 `tools/patch_tgl_vadriver_h264.py` 清零 `MEDIA_SURFACE_STATE.DW2`（`0x5f048`）后，标准 VideoToolbox H.264 硬编（`feature=0x4000`）已在 `1728×1080` 与 `2880×1800` 下验证画质与率控正常，无花屏或码率失控。
- **原生低延迟 H.264 编码（继续屏蔽）**：`feature=0x400`（VDENC 低延迟速率控制）会触发 VCS0 队列挂起，因此保持通过 `-12903` 拦截使客户端自动走已修复的标准硬编路径；完整原生低延迟编码与 HEVC 硬编仍待后续独立定位。
- **Metal 内存增长（已显著缓解）**：`WhateverXe-Graphics v0.4.4`（`#47` 修复）部署后，常规桌面下 `WindowServer` 内存占用已稳定在 ~`190 MB` 级，但长期高压测与更多应用组合仍需继续观测。
- **渲染队列偶发挂起（未完全根除）**：虽然 `WhateverXe 0.8.3` 合入了 GPU 挂起后的关机/重启恢复修正（`TGLRecovery` 等），重负载（如部分桌面应用金属合成）下仍曾观测到极少数 `RCS+BCS` 队列卡滞与 `WindowServer` watchdog 重启；调度、`CSB` 及资源链表热点仍在定位中。

## 外接显示与睡眠

- **机身 HDMI 输出与 HiDPI（日常已通，长期场景待续测）**：使用 `-wex-edp-hdmi-only` 结合显示模式精简（零碰撞 15 项 `scale-resolutions`）后，外屏 `3840×2160`（UI `1920×1080 HiDPI @ 144Hz`）与内屏 `2880×1800`（UI `1440×900 HiDPI @ 120Hz`）可同屏稳定使用，且系统设置“显示器”已秒开无卡死。
- **长睡眠与 S0ix（仍待分段验收）**：`WhateverXe 0.8.3-air14` 已集成 `restoreAfterPowerLoss`、`deferRearm`、`finishDisplay13SystemWake`、`restoreGGTT` 及本机退场/热插拔保护；日常合盖/开盖与常规重启用例可工作，但“仅内屏 / 接 HDMI × 锁屏 / 桌面 × AC / 电池”全矩阵多轮电源键短按、整晚合盖功耗（Deep Idle/S0ix）及 DarkWake 仍需继续实机采集验收。
- **USB-C DP Alt Mode / 雷电**：普通 USB 扩展坞设备可枚举，USB-C 视频输出与 Thunderbolt NHI 热插拔尚未解决。

## 外设与系统稳定性

- **隔空投送与蓝牙**。AX211 空投已在 25G332 完成一次发送和一次接收。长时间运行、较大文件、多轮长睡眠后的回连和 A2DP/HFP 音频并发仍待验证。旧版 `IntelBTUnified` 可作为独立蓝牙回退配置。[实机记录](status-2026-11-26.md)
- **音频**。内置数字麦克风和 3.5 mm 耳麦已实测可录音。内置扬声器、长睡眠后的录音和不同采样率仍待验证。[音频记录](status-2026-11-27.md)
- **原生长时间日志写盘压力**：若系统存在遗留诊断过滤或高频 `SkyLight` Signpost 持久化，叠加双屏高刷可能引发 `logd` watchdog 超时；见 [构建与配置](setup.md#系统日志写盘降载logd-watchdog-防护) 关闭持久化并清理旧测试守护进程。
- **原生 UVC 摄像头**：常规预览、应用访问和 90 秒持续原始帧采集已验证；IR 红外摄像头与长时间通话未验证。
- **CPU 温度与热键**：封装温度读数已验证；独立风扇 RPM、联想 EC/DYTC 模式切换、`Fn+Q`/`Fn+R`/`Fn+Space`/FnLock 等更多物理热键仍未完整适配。
- **Apple 账户与 App Store**。系统设置登录与 iCloud 账户保存已验证。App Store 下载与 Siri 待验。
