# 构建与配置

## 显卡源码（WhateverXe 0.8.3-air14 + Graphics v0.4.4）

`sources/WhateverXe` 保留基于 WhateverXe `0.8.3` 的源码、独立 CPU/mock 测试、睡眠唤醒/掉电恢复机制及本机适配修改。源目录中的英日 README 说明通用组件与要求；本仓库首页和已知问题描述 Air14 的实际验证范围。

安装 Xcode Command Line Tools，准备独立的 [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) 与 Lilu 开发资源。构建脚本要求 `Lilu.kext/Contents/Resources/Library/plugin_start.cpp` 存在；普通运行版 kext 未必附带这些资源。

```sh
cd sources/WhateverXe
python3 tools/build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
python3 tools/test.py
```

产物位于该源码目录的 `build/`。构建不会修改正在运行的 EFI，也不会安装 Apple 图形驱动。

`WhateverXe 0.8.3-air14` 发布 `WhateverXeGraphicsABI = 1`，**必须**与配套的 [WhateverXe Graphics v0.4.4](https://github.com/b00t0x/WhateverXe-Graphics)（内置 `AppleIntelTGLCompat 0.8.1`）成对安装使用，切勿混用 `0.7.0` 与 `0.4.4` 或 `0.8.3` 与 `0.4.0`。WhateverXe 应在 Lilu 之后加载，不能同时启用 WhateverGreen。系统设置与 SIP 要求按 [上游安装说明](../sources/WhateverXe/README.md#installation) 处理。本仓库不分发 Apple 驱动二进制。

## H.264 硬件编码修补与自检

在 macOS Tahoe `25G332` 下直接安装原版 `WhateverXe-Graphics v0.4.4` 的 `AppleIntelTGLGraphicsVADriver.bundle` 后：
1. 原生低延迟 H.264 编码（`feature=0x400`）会触发 VCS0 队列挂起与 GPU 重置；
2. 标准 H.264 硬件编码（`feature=0x4000`）因 `MEDIA_SURFACE_STATE.DW2`（文件偏移 `0x5f048`）未在 patch-list 路径前清零步幅，出现严重码率膨胀与画面花屏残影。

仓库提供针对 stock `0.4.4` `AppleIntelTGLGraphicsVADriver.bundle` 的可复现修补脚本与验证工具：

```sh
# 1. 在保留 feature=0x400 拦截的前提下修复标准 H.264 硬件编码（默认 hw-encode-fix 模式）
python3 tools/patch_tgl_vadriver_h264.py \
  --src /path/to/stock-0.4.4/AppleIntelTGLGraphicsVADriver.bundle \
  --dst ./AppleIntelTGLGraphicsVADriver.bundle \
  --mode hw-encode-fix

# 如需保守纯软件编码模式，改用 --mode sw-fallback（仅保留 H.264 硬解与 HEVC）
```

> 安装前请先备份 `/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle`，以 `root:wheel` 权限替换后验证 `codesign --verify --deep --strict`，并重启 `VTEncoderXPCService` / `VTDecoderXPCService` 即可生效（无需整机重启）。

使用内置测试套件校验编码输出（自包含 VideoToolbox 编码/软解还原、Y/U/V PSNR 与 Annex-B Slice QP 解析）：

```sh
clang++ -std=c++17 -O2 -Wall -Wextra -fobjc-arc \
  -framework Foundation -framework VideoToolbox -framework CoreVideo \
  -framework CoreMedia -framework CoreGraphics -framework ImageIO \
  tools/verify_vt_h264.mm -o ./build/verify_vt_h264

python3 tools/run_verify_vt_h264.py --tool ./build/verify_vt_h264 --out ./build/vt-report
```

## 蓝牙（IntelBTUnified + AX211 固件注入）

在 macOS Tahoe 上，使用 `BlueToolFixup` 对 `/usr/sbin/bluetoothd` 打内存补丁在 APFS 压缩页重读时会导致页签名异常与 `SIGBUS` 反复崩溃。可改用 [IntelBTUnified](https://github.com/laguillo/IntelBTUnified) `v1.0.1`（替代 `IntelBluetoothFirmware` + `IntelBTPatcher` + `BlueToolFixup`）通过 `_vnode_pagein` 规避页签名崩溃。

针对 AX211 (`8087:0033`)，因 `IntelBTUnified 1.0.1` 原包内嵌 `2023-10-17` 固件在 Tahoe `25G332` 缺少 LE 命令掩码会导致 `OI_HCICMD_LE_Read_BufferSize_V2 (status=628)` 报错退出，先从自备的 `IntelBluetoothFirmware 2.5.3` 提取 `2026-01-28` / `2024-01-31` 固件注入到副本后再装入 EFI：

```sh
python3 tools/patch_intelbtunified_ax211_fw.py \
  /path/to/copy/IntelBTUnified.kext/Contents/MacOS/IntelBTUnified \
  /path/to/IntelBluetoothFirmware-2.5.3.kext/Contents/MacOS/IntelBluetoothFirmware
```

## 配置片段

[`config/OpenCore-fragment.plist`](../config/OpenCore-fragment.plist) 不是完整可启动配置，只包含以下本机适配项：

- `SSDT-AIR-PNLF.aml` 与 `SSDT-AIR-I2CP.aml` 两条 ACPI Add 记录。
- 对本机 DSDT 中 TPD0 `_CRS` 的精确重命名。限定 DSDT 长度、OEM Table ID、完整方法字节及一次匹配。
- 本机固定 DVMT 设置对应的 framebuffer 属性与 I2C polling 属性。
- 本机使用的相关启动参数（默认不含实验性 `-wex-video-progressive-fix`）。
- 当前显示恢复候选使用的 `UEFI → Output → ReconnectGraphicsOnConnect = true`。

需逐项合并到已有 OpenCore 配置，保留其其他必要项。片段中的 `boot-args` 也需要合并，不能直接覆盖已有参数。对不同固件必须重新检查 ACPI 路径和匹配条件，不能把重命名改成全局 `_CRS` 替换。

当前 HDMI 路径使用 `-wex-edp-hdmi-only`，根据连接器和显示器能力协商 HDMI 2.0。使用这条路径时移除 `-wex-edp-panel-only`；后者保留为内屏恢复策略，会优先禁用外屏。`-igfxdvmt` 与片段里的 framebuffer 内存属性针对本机固定预分配情况，不是适用于所有机器的通用内存扩容设置。

> 注：`WhateverXe 0.8.3` 已通过 OpRegion Mailbox 5 原生识别本机内屏 EDID 身份，日常不再需要额外手工注入 `xelp-panel-edid`。仅旧基线或回退排查时参考 [panel identity selection](../sources/WhateverXe/docs/device-selection.md#firmware-selected-internal-panel)。

## 原生热键

使用 VoodooPS2Controller 的键盘插件处理 ACPI 通知，无需 Air14 Hotkeys App。编译并安装自定义 SSDT：

```sh
mkdir -p build/acpi
iasl -p build/acpi/SSDT-BKEY ACPI/SSDT-BKEY.dsl
```

将 AML 放入已有 EFI 的 `OC/ACPI`，逐项合并 [`config/Air14-hotkeys-fragment.plist`](../config/Air14-hotkeys-fragment.plist) 的一条 Add 和两条 Patch。片段限定本机 DSDT 长度、OEM Table ID，并各重命名一次 `_Q8E` / `_Q8F`；其他固件必须先检查路径和唯一匹配，不可移除这些限制后全局替换。

Darwin 下亮度事件通过 PS2K 转发；其他系统调用原 EC 方法。RMCF 同时注入 Insert → Help/Insert、Print Screen → F13，不需要发布或替换第三方驱动二进制。更新 FAT32 格式 ESP 时务必使用 `COPYFILE_DISABLE=1` 避免写入 `._*` AppleDouble 元数据文件破坏 kext 包结构。

## 外屏 HiDPI 与防设置卡死

`tools/make_hidpi_override.py` 根据显示器 vendor/product ID 生成仅含 `scale-resolutions` 的候选文件，不覆盖 EDID 也不修改时序。

```sh
python3 tools/make_hidpi_override.py \
  --vendor 0x1234 --product 0x5678 \
  --logical 1920x1080 \
  --lodpi 1920x1080 \
  --output DisplayProductID-5678
```

实机排查证实两个关键限制（已在最新版 `make_hidpi_override.py` 中强制防护）：
1. **避免模式列表膨胀**：向 `scale-resolutions` 写入过多（例如超过 32~200 项）分辨率会使 `MonitorPanel.framework` 通过 `WindowServer` 同步查询耗时呈 $O(N^2)$ 增长，导致系统设置“显示器”空白卡死 15–40 秒。本机已验证将外屏精简为 15 项（6 项 LoDPI + 9 项 HiDPI）后，`MPDisplayMgr` 初始化耗时降至 `0.545 s`，设置面板秒开。请勿在 BetterDisplay 中开启生成海量阶梯模式的配置。
2. **禁止同背板 LoDPI/HiDPI 碰撞**：不要在 16 字节 `0x200000` HiDPI 条目之前放置相同背板分辨率（如 `3840×2160`）的 8 字节/12 字节 LoDPI 条目，否则 `IOFramebuffer` 会丢失 `0x00200000` 标志并导致系统拒绝切换对应 HiDPI 模式（例如 `1920×1080 HiDPI @ 144Hz`）。

示例 ID 必须替换为自己显示器的实际值，安装位置为 `/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-<十六进制 vendor>/DisplayProductID-<十六进制 product>`；先备份已有文件再重新连接显示器或重启，删除新增文件并重连即可撤销。

## 背光

使用 ACPICA 的 `iasl` 编译自定义 SSDT：

```sh
mkdir -p build/acpi
iasl -p build/acpi/SSDT-AIR-PNLF ACPI/SSDT-AIR-PNLF.dsl
iasl -p build/acpi/SSDT-AIR-I2CP ACPI/SSDT-AIR-I2CP.dsl
```

PNLF 源码只对 Darwin 暴露设备，不写旧版 PWM MMIO。本机使用 `_UID=19`，并由 WhateverXe 匹配 `F19T1416` 面板背光配置。其他面板需重新验证。

## 触控板与电池

触控板采用 VoodooI2C 2.9.1、VoodooI2CHID、polling SSDT 和本机 I2C 控制器匹配注入器。SSDT 保留其他操作系统原有 `_CRS` 行为；注入器仅匹配 `8086:51E8`。

将 [`config/AIR14-I2C-D0.Info.plist`](../config/AIR14-I2C-D0.Info.plist) 放到自行建立的 `AIR14-I2C-D0.kext/Contents/Info.plist`。这是不含可执行文件的匹配注入器，仍依赖 VoodooI2C 实际驱动，加载顺序位于 VoodooI2C 后、VoodooI2CHID 前。

电池使用 VirtualSMC、ECEnabler 和 SMCBatteryManager；本机验证版本分别为 1.3.9、1.0.6、1.3.8。它们的二进制和许可证从各自上游获取。

## 120Hz 与内屏 EDID 校验

本机物理面板支持 2880 × 1800 的 120Hz 时序。原始 EDID 的外层块校验正确，DisplayID 内层校验错误，导致系统跳过扩展中的时序。

```sh
python3 tools/fix_panel_edid.py \
  --edid ./panel-edid.bin \
  --output ./DisplayProductID-1416
```

目标覆盖路径为 `/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-e6f/DisplayProductID-1416`。先备份已有文件，安装后重启再在系统显示设置中选择 120Hz。

## 系统日志写盘降载（`logd` watchdog 防护）

若外接高刷屏幕合并后台高 GPU 合成负载时观察到 `SkyLight` Signpost 高频写盘或 `logd` watchdog 超时，可通过系统官方命令关闭两类高频 `SkyLight` signpost 持久化并重置遗留内核日志过滤：

```sh
sudo log config --process kernel --reset
sudo log config --subsystem com.apple.SkyLight --category tracing.stalls --mode signpost-persisted:off
sudo log config --subsystem com.apple.SkyLight --category performance_instrumentation --mode signpost-persisted:off
```
