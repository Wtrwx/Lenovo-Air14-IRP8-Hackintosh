# 构建与配置

## 显卡源码

`sources/WhateverXe` 保留 WhateverXe 0.7.0 的源码、独立 CPU/mock 测试及本机适配修改。源目录中的英日 README 说明通用组件与要求；本仓库首页和已知问题描述 Air14 的实际验证范围。

安装 Xcode Command Line Tools，准备独立的 [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) 与 Lilu 开发资源。构建脚本要求 `Lilu.kext/Contents/Resources/Library/plugin_start.cpp` 存在；普通运行版 kext 未必附带这些资源。

```sh
cd sources/WhateverXe
python3 tools/build.py --release --sdk /path/to/MacKernelSDK --lilu /path/to/Lilu.kext
python3 tools/test.py
```

产物位于该源码目录的 `build/`。构建不会修改正在运行的 EFI，也不会安装 Apple 图形驱动。

WhateverXe 应在 Lilu 之后加载，不能同时启用 WhateverGreen。图形驱动包、Mod AMFIPass、系统版本和 SIP 设置按 [上游安装要求](../sources/WhateverXe/README.md#installation) 配套处理。此仓库没有附带 Apple 驱动文件。

## 配置片段

[`config/OpenCore-fragment.plist`](../config/OpenCore-fragment.plist) 不是完整可启动配置，只包含以下本机适配项：

- `SSDT-AIR-PNLF.aml` 与 `SSDT-AIR-I2CP.aml` 两条 ACPI Add 记录。
- 对本机 DSDT 中 TPD0 `_CRS` 的精确重命名。限定 DSDT 长度、OEM Table ID、完整方法字节及一次匹配。
- 本机固定 DVMT 设置对应的 framebuffer 属性与 I2C polling 属性。
- 本机使用的相关启动参数。

需逐项合并到已有 OpenCore 配置，保留其其他必要项。片段中的 `boot-args` 也需要合并，不能直接覆盖已有参数。对不同固件必须重新检查 ACPI 路径和匹配条件，不能把重命名改成全局 `_CRS` 替换。

当前 HDMI 路径使用 `-wex-edp-hdmi-only`，根据连接器和显示器能力协商 HDMI 2.0。使用这条路径时移除 `-wex-edp-panel-only`；后者保留为内屏恢复策略，会优先禁用外屏。已实测内屏 120Hz 与外屏 1440p 144Hz 同时工作、外屏颜色正常；开机后仍可能需要重插 HDMI，详见[已知问题](known-issues.md)。`-igfxdvmt` 与片段里的 framebuffer 内存属性针对本机固定预分配情况，不是适用于所有机器的通用内存扩容设置。

本机 VBT 使用固件选择的面板标识，需要额外提供 `xelp-panel-edid`：在 iGPU 上添加从**自己面板**取得并校验的 **128 字节 EDID base block**，类型为 Data。出于隐私和硬件匹配要求，仓库未附带该属性值。具体准入规则见 [panel identity selection](../sources/WhateverXe/docs/device-selection.md#firmware-selected-internal-panel)。

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

将 [`config/AIR14-I2C-D0.Info.plist`](../config/AIR14-I2C-D0.Info.plist) 放到自行建立的 `AIR14-I2C-D0.kext/Contents/Info.plist`。这是不含可执行文件的匹配注入器，仍依赖 VoodooI2C 实际驱动。它不是独立的 I2C 驱动，也不是通用的强制 D0 修复。加载顺序保留 VoodooI2C 的服务及插件要求，使该注入器在 VoodooI2C 后、VoodooI2CHID 前。

电池使用 VirtualSMC、ECEnabler 和 SMCBatteryManager；本机验证版本分别为 1.3.9、1.0.6、1.3.8。它们的二进制和许可证从各自上游获取。没有附带固件 DSDT、通用 EC 风扇补丁或温度插件安装方案。

## 120Hz 与 HiDPI

本机物理面板支持 2880 × 1800 的 120Hz 时序。原始 EDID 的外层块校验正确，DisplayID 内层校验错误，导致系统跳过扩展中的时序。

工具只接受 `E6F:1416`、256 字节 EDID、单个指定布局的 DisplayID 扩展。它只重算该布局的内部校验字节和外层扩展校验字节，保留所有时序及 base block，不制造新的 120Hz 时序。

```sh
python3 tools/fix_panel_edid.py \
  --edid ./panel-edid.bin \
  --output ./DisplayProductID-1416
```

如已有可用的显示覆盖，可通过 `--base-override ./existing-override.plist` 保留其 HiDPI/scale 配置。工具只生成候选文件，不自动安装或修改系统。HiDPI 配置不是该校验工具自动生成的内容。

完成本地核对后，目标覆盖路径为 `/Library/Displays/Contents/Resources/Overrides/DisplayVendorID-e6f/DisplayProductID-1416`。先备份该路径已有文件，安装后重启，再在系统显示设置中选择 120Hz。恢复时还原原有覆盖；若此前没有覆盖则移除新文件。

**系统显示覆盖中的 256 字节 EDID 与 EFI 的 `xelp-panel-edid` 不是同一个用途。** 后者保持物理面板的 128 字节 base block，不能用完整显示覆盖内容替换。

本机已验证 1440 × 900 HiDPI 与约 120.0002Hz 的实际输出。120Hz 可用不表示 GPU 挂起或动画卡顿已修复。
