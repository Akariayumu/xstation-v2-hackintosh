# S0ix 实验 U 盘 EFI 确定性构建工具（Phase 0 / Phase 1）

配套路线图：[`docs/research/s0ix-roadmap-20261115.md`](../../docs/research/s0ix-roadmap-20261115.md)  
配套 ACPI 源码：[`acpi/SSDT-DEEPIDLE.dsl`](../../acpi/SSDT-DEEPIDLE.dsl) → `EFI/OC/ACPI/SSDT-DEEPIDLE.aml`

`stage_usb_efi.py` 仅依赖 Python 3 标准库（`plistlib` / `pathlib` / `hashlib` / `zipfile`），用于把仓库基线 `EFI/` 安全克隆并转换为 **Phase 0** 或 **Phase 1** 救援/实验 U 盘 `EFI/`，全程**不修改仓库内的生产 `EFI/OC/config.plist`**。

## 安全保障（Zero-Brick Safety Contract）

1. **拒绝覆盖生产目录**：自动拦截指向仓库 `EFI/` 或 `/Volumes/OPENCORE` 的输出路径；目标 `<out>/EFI` 已存在时默认拒绝覆盖（需显式加 `--force`）。
2. **强制关闭 SPI NVRAM 落盘**：生成的 U 盘 `config.plist` 强制设 `NVRAM -> WriteFlash = false`（保留 `Delete -> boot-args` 仅作用于当次内存引导），拔掉 U 盘重启即 100% 恢复生产启动环境。
3. **解除 `24.6.0` 内核硬锁**：自动将 `Kernel -> Add` 中锁死在 `24.6.0` 的 14 项驱动放宽至 `24.0.0` – `24.99.99`。
4. **原始固件 DSDT 二进制校验（`--verify-dsdt`）**：直接对本机提取的 `dsdt.dat`（`359,989` 字节，`OemTableId = "A M I \0\0"` / `41204D2049200000`）模拟执行全部启用的 `DSDT` 补丁，校验 `_S3_ -> XS3_`（偏移 `0x06055`）与 `PEPD._STA -> XSTA`（偏移 `0x3B58D`）均为 `count == 1` 全局唯一匹配。

## 上游组件下载与 SHA-256 校验值

> **注意**：
> 1. 上游 `S0ixStandby` 发布页自带的 `SSDT-DEEPIDLE.aml`（`161` 字节）**不含**本机必需的 `\_SB.PEPD._STA` Darwin 暴露逻辑，请务必使用本仓库编译的 `EFI/OC/ACPI/SSDT-DEEPIDLE.aml`（`175` 字节，SHA-256 `47130b2c887514830aa7731b74be5560d14405ec57bc940988beebd3455b72b7`），`stage_usb_efi.py --phase 1` 会自动启用它。
> 2. 本机 Sharp 4K 面板（`SHP14A8`）必须使用打上 [`patches/WhateverXe/wex-0.8.2-xstation-v1.patch`](../../patches/WhateverXe/wex-0.8.2-xstation-v1.patch) 编译出的定制 `WhateverXe.kext`，切勿直接混用未打补丁的上游原版 `WhateverXe-0.8.2-RELEASE.zip`。

| 组件 | 版本 | 文件名 | SHA-256 | 下载地址 |
| :--- | :--- | :--- | :--- | :--- |
| **S0ixStandby**（推荐） | `v0.5.4` | `S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Release.zip` | `266598d8613bf841568a0611c6b463fcdff7aac9afd27d7b036c84f34ca01553` | https://github.com/labrouss/S0ixStandby/releases/download/v0.5.4/S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Release.zip |
| **S0ixStandby**（调试版） | `v0.5.4` | `S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Debug.zip` | `3034ec025c30eaf92eee68e2629bc123707174a732ac0193a8cbe21ba909470c` | https://github.com/labrouss/S0ixStandby/releases/download/v0.5.4/S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Debug.zip |
| **WhateverXe Graphics** | `v0.4.4` | `WhateverXe-Graphics-0.4.4.pkg` | `f4c2d680a7c52c33a3f038ce6945da0aa2cc044e62b935a52ea046c7c473cfec` | https://github.com/b00t0x/WhateverXe-Graphics/releases/download/v0.4.4/WhateverXe-Graphics-0.4.4.pkg |
| **WhateverXe Graphics**（AutoPkg） | `v0.4.4` | `AutoPkg-Assets.pkg` | `415e2de4406244a358d3b6f96320389095fc7b2c84692665a1328fd652aab0fa` | https://github.com/b00t0x/WhateverXe-Graphics/releases/download/v0.4.4/AutoPkg-Assets.pkg |
| **WhateverXe**（上游源码参考） | `0.8.2` | `WhateverXe-0.8.2-RELEASE.zip` | `6c004bdfd9c6ce369dcb4dde515fc16e893345061dc63eb0302634ad7d8525b8` | https://github.com/b00t0x/WhateverXe/releases/download/0.8.2/WhateverXe-0.8.2-RELEASE.zip |

`S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Release.zip` 解包后内部文件校验值：
- `S0ixStandby.kext/Contents/Info.plist`（`4,668` B）：`23fcd6f0a5af97aa58e51798e34ea179540443d4319c54e0989f3762d4a7a0f4`
- `S0ixStandby.kext/Contents/MacOS/S0ixStandby`（`73,384` B）：`6eba0d32066ab0fed8ed7e2c20d2a5d65d2c4dd01b9523a7aa1f0232bf60991b`

## 快速使用

### 0. 前置准备（首次拉取仓库第三方组件）

若尚未下载 OpenCore 与第三方 kext 二进制，请先在仓库根目录执行：

```bash
bash scripts/fetch-thirdparty.sh
```

（实机启动前请确保源 `EFI/OC/config.plist` 或目标 U 盘 `config.plist` 已填入你自己的有效 SMBIOS 序列号。）

### 1. 生成 Phase 0 实验 U 盘 EFI（图形栈升级基线，保持 `SleepDisabled=1`）

将编译好的 `WhateverXe 0.8.2 + wex-0.8.2-xstation-v1.patch` 产物传入 `--wex-kext`（支持 `.kext` 目录或 `.zip` 包），保持现役 `boot-args`，不启用 `SSDT-DEEPIDLE.aml`：

```bash
python3 tools/s0ix/stage_usb_efi.py \
  --phase 0 \
  --out /Volumes/USB_EFI \
  --wex-kext /path/to/build/Release/WhateverXe.kext
```

安装系统盘 `WhateverXe-Graphics-0.4.4.pkg` 前，请先按路线图 §1.3 执行 `tmutil localsnapshot` 并备份 `/Library/Extensions/AppleIntelTGL*`。

### 2. 生成 Phase 1 实验 U 盘 EFI（纯平台 Deep Idle `SleepType=7` 破冰）

自动写入 `_S3_ -> XS3_` 与 `PEPD._STA -> XSTA` 两条 DSDT 补丁、启用 `SSDT-DEEPIDLE.aml`、注入 `S0ixStandby.kext`（支持传入 `.kext` 目录或官方 `Release.zip`），并在保留全部 5 个 Sharp 4K 帧缓冲守卫参数的基础上追加 `-wex-noaccel -s0ixdbg`（即 `-v keepsyms=1 debug=0x100 -allow3d -disablegfxfirmware alcid=19 alcapplid=19 -wex-tgl-sdt-guard -noDC9 -nodisplaysleepDC6 -wex-no-panel-keep -wex-sink-rate-restore -wex-noaccel -s0ixdbg`；因为 `-wex-noaccel` 仅禁用 3D 加速器 `AppleIntelTGLGraphics`，`AppleIntelTGLGraphicsFramebuffer` 仍会加载驱动内屏，缺失 `-wex-tgl-sdt-guard` 会在光标初始化 `computeWatermarkValue` 时触发除零内核崩溃）：

```bash
python3 tools/s0ix/stage_usb_efi.py \
  --phase 1 \
  --out /Volumes/USB_EFI \
  --wex-kext /path/to/build/Release/WhateverXe.kext \
  --s0ix-kext /path/to/S0ixStandby-0.5.4-v0.5.3-5-gc52f172-Release.zip \
  --verify-dsdt /path/to/dsdt.dat \
  --force
```

## 运行单元测试

```bash
python3 -B -m unittest discover -s tools/s0ix -v
```
