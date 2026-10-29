# X-STATION V2 Hackintosh（OpenCore）

i7-1165G7 / Iris Xe 二合一平板电脑上的 macOS Sequoia OpenCore EFI、ACPI 补丁和完整适配记录。

> **实验性项目。** 显卡依赖仍在开发中的 [WhateverXe](https://github.com/b00t0x/WhateverXe)，
> 出现过 GPU 反复重启后 panic 的情况（见[已知问题](#已知问题)）。
> 请先用 U 盘启动验证，保留原系统和数据备份。本项目与 Apple、Intel 及整机厂商无关。

当前版本：**EFI v0.1.55**（2026-10-29），macOS Sequoia 15.8.1（24H32），OpenCore 1.0.8，SMBIOS `MacBookPro16,2`。

## 硬件

| 项目 | 型号 |
|---|---|
| 机型 | X-STATION V2 |
| CPU | Intel Core i7-1165G7（Tiger Lake-UP3） |
| 显卡 | Intel Iris Xe `8086:9A49` |
| 屏幕 | 13.4" 3840×2400 eDP，Wacom AES 触控与手写笔（`056A:48EE`，I²C HID） |
| 硬盘 | Kingston OM8PDP31024B 1 TB NVMe |
| 无线 | Intel AX201（Wi-Fi 6 + 蓝牙） |
| 音频 | Realtek ALC256 |
| 接口 | 2 × 雷电 4（按 USB-C 使用）、1 × USB 3.2 Type-C |

详细的 PCI / ACPI / USB 身份见 [docs/hardware.md](docs/hardware.md)。

## 适配状态

| 设备 | 状态 | 说明 | 起始版本 |
|---|---|---|---|
| 核显加速 | ✅ | Metal，3840×2400 面板按 2560×1600 HiDPI | B′ 基线 |
| 背光 / 关屏点亮 | ✅ | SSDT-PNLF；关屏后点亮反复测试通过 | v0.1.42 |
| 触摸屏 | ✅ | VoodooI2C 2.9.1，10 点，GPIO 中断 | v0.1.41 / v0.1.52 |
| 手写笔 | ✅ | 悬停、笔尖、侧键、橡皮擦；应用内压感未验证 | v0.1.51 |
| 键盘 / 音量键 | ✅ | VoodooPS2（只用 Keyboard 插件） | v0.1.42 |
| 电源键 | ✅ | SSDT-PWRBEC；短按弹出关机面板需配套小工具（`tools/powerkey`） | v0.1.50 |
| 扬声器 / 耳机 / 麦克风 | ✅ | AppleALC layout 19 + coef `0x10` 扬声器开关 | v0.1.44 |
| Wi-Fi | ✅ | AirportItlwm（原生 Wi-Fi 菜单） | v0.1.36 |
| 蓝牙 | 🟡 | 可用，登录前偶尔连不上 | v0.1.40 |
| 电池 | ✅ | 电量与充电状态 | v0.1.42 |
| CPU 电源管理 | ✅ | XCPM + HWP，0.4–4.7 GHz；关屏空闲整机约 3.8 W（插座端） | — |
| NVMe | ✅ | NVMeFix + ASPM L1 | v0.1.53 |
| USB | ✅ | 实测定制映射（UTBMap） | v0.1.55 |
| 摄像头 ×2 | 🟡 | QuickTime 可用；Photo Booth 预览崩溃 | — |
| 读卡器 | ⬜ | 已识别，未插卡测试 | — |
| 雷电 | ❌ | 集成雷电（NHI）在 macOS 无驱动，接口只按 USB 使用 | — |
| 系统睡眠 | ❌ | 睡眠路径会 panic，已禁用；用关屏代替 | — |
| 指纹 | ❌ | 无法适配 | — |
| 外接显示器 | ⬜ | 未测 | — |

## 已知问题

- **GPU 可能卡死后 panic。** 动态壁纸（WebKit 播放视频）与远程桌面截屏同时运行时，出现过 GPU 反复重启
  → WindowServer 看门狗 → panic。日志特征为 `GPURestart` 与
  `userspace watchdog timeout: no successful checkins from WindowServer`。根因尚未定位；
  两者都不运行时连续观察 45 分钟没有 GPU 错误。
- **系统睡眠已禁用**（`pmset SleepDisabled=1`）。合盖和菜单里的“睡眠”都不生效。
- **关屏点亮靠绕开 DC9/DC6**（`-noDC9 -nodisplaysleepDC6`），不是修复了低功耗状态的退出流程。
- **触摸驱动偶发 panic**：VoodooI2C 在 `forwardReport` 中读 `IOFBTransform` 时没有判空，很少触发。
- **Photo Booth 打开预览即崩溃**，录像请用 QuickTime。

## 版本历史

每个版本只改一个变量。[CHANGELOG.md](CHANGELOG.md) 只收录有实际突破的版本，每条都写明当时卡在哪里、解决思路和具体改动。下面是主线：

| 阶段 | 版本 | 卡在哪里 | 怎么解决的 |
|---|---|---|---|
| 引导 | v0.1.0–v0.1.15 | `EXITBS:START` 卡死；Recovery 里键鼠无效 | 按同 CPU 的 Yoga 13ITL5 对齐 quirks（v0.1.11）、无 MAT 的 Booter 组合（v0.1.14）、USBToolBox 临时映射（v0.1.15） |
| 显示（Tahoe） | v0.1.16–v0.1.33 | WindowServer 在 `SetupTimings` / watermark 计算里除零 panic | AGDC 回调失败后时序没初始化；WhateverXe 补丁写回已验证的 4K 时序（v0.1.33） |
| 加速（Tahoe） | v0.1.34–v0.1.35 | WindowServer 每 9 秒崩溃（`Xe compiler unsupported bitcode layout`） | 着色器转换器不支持 CoreAnimation 的一个变体，改回 Sequoia |
| 加速（Sequoia） | B′ 基线 | 官方 0.5.0 在 Sequoia 同样除零 | 时序修复移植到 Sequoia，4K + Metal 2 稳定 |
| 外设 | v0.1.36–v0.1.41 | 无 Wi-Fi / 蓝牙 / 声音 / 触摸 | AirportItlwm、Intel 蓝牙套件、AppleALC、VoodooI2C |
| 关屏点亮 | v0.1.42 | 关屏后黑屏，链路训练失败 | 面板断电会丢失 DPCD `0x115` 速率表；训练前写回（`-wex-sink-rate-restore`） |
| 扬声器 | v0.1.43–v0.1.45 | 耳机有声，扬声器无声 | Linux 反向验证：ALC256 coef `0x10` bit9 是扬声器静音位，写进 layout 19 |
| 电源键 | v0.1.46–v0.1.50 | macOS 下按电源键没有任何事件 | EC 地址空间没被读过就不发 Query `0x54`；`_REG` 时补读一遍 EC RAM |
| 手写笔 | v0.1.51–v0.1.52 | 笔数据被丢弃；触摸退回 5 ms 轮询 | VoodooI2C 2.9.1；上游 CI 版 VoodooGPIO（Tiger Lake LP） |
| 功耗 / USB | v0.1.53–v0.1.55 | 关屏功耗偏高；USB 用的是临时全开映射 | NVMeFix；逐口插拔实测生成 UTBMap |

### 按症状查

| 你遇到的现象 | CHANGELOG 里看 |
|---|---|
| 卡在 `EXITBS:START` | v0.1.11：拿同 CPU 能用的 EFI 成组对齐 quirks |
| 进内核后 page fault / 重启 | v0.1.14：固件不支持 MAT 时用无 MAT 的 Booter 组合 |
| Recovery / 安装界面键鼠没反应 | v0.1.15：USBToolBox + 临时全开映射 |
| `SetupTimings+0x159` 或 `computeWatermarkValue` 除零 panic | v0.1.33、B′ 基线：写回已验证的时序 |
| 开机偶发 `setupPipeWatermarks` panic、关屏后 `PG2 power well` panic | 未编号（10-27）：基于 0.5.0 源码的最小补丁 |
| 关屏后再点亮黑屏 | v0.1.42：绕开 DC9/DC6 + 写回 DPCD `0x115` |
| 亮度滑块无效 | 未编号（10-27）：SSDT-PNLF 挂在 `\_SB.PC00.GFX0` |
| layout 改了不生效、扬声器无声 | v0.1.44：`alcapplid` 与 coef `0x10` |
| 电池不显示 | 未编号（10-26）：SMCBatteryManager **加** ECEnabler |
| 电源键无效 | v0.1.50：`_REG` 时读一遍 EC RAM |
| 笔不工作 | v0.1.51：VoodooI2C 2.9.1 |
| 触摸 CPU 占用高（轮询） | v0.1.52：Tiger Lake LP 的 VoodooGPIO |

## 目录

```text
EFI/                      OpenCore EFI。第三方文件由脚本补齐，见下
  OC/config.plist         已脱敏：SMBIOS 是占位符，不能直接启动
  OC/ACPI/                本机 SSDT（AML）
  OC/Kexts/UTBMap.kext    本机 USB 端口映射
  OC/Kexts/XSHIDDLog.kext 本项目的 HIDD 日志 kext（源码在 src/）
  OC/Kexts/WhateverXe.kext            WhateverXe 0.5.0 + 本项目补丁的编译结果
  OC/Kexts/VoodooI2C.kext/.../VoodooGPIO.kext  上游 CI 构建（GPL-3.0，见 THIRD_PARTY.md）
  OC/Kexts/{AppleALC,VoodooI2C}.kext/Contents/Info.plist  在上游版本上改过的 Info.plist
acpi/                     SSDT 源码（.dsl），编译结果与 EFI 中的 AML 一致
patches/WhateverXe/       对 WhateverXe 0.5.0 的补丁和说明
src/XSHIDDLog/            XSHIDDLog.kext 源码
scripts/fetch-thirdparty.sh  下载并校验第三方组件，补齐 EFI
thirdparty.lock           第三方发布包的 URL / SHA-256，以及每个文件的来源
EFI-sha256.txt            v0.1.55 EFI 中每个文件的 SHA-256（config.plist 除外）
tools/                    macOS 下用的小工具（电源键面板、USB 插拔监视、手写笔报告抓取、电池功耗）
docs/                     硬件信息与各子系统的调查记录
CHANGELOG.md              各次突破的问题、思路和改动
THIRD_PARTY.md            第三方组件、版本和许可证
```

## 使用方法

1. **补齐第三方组件。**

   ```bash
   scripts/fetch-thirdparty.sh
   ```

   脚本从各项目的 GitHub 发布页下载 14 个固定版本的发布包，逐个校验 SHA-256，取出 44 个文件放进 `EFI/`，
   最后按 `EFI-sha256.txt` 校验 EFI 里全部 60 个文件。通过后得到的 EFI 与作者实机使用的 v0.1.55 逐字节一致
   （config.plist 除外）。Linux 和 macOS 都能运行。

2. **生成自己的 SMBIOS。** 用 [GenSMBIOS](https://github.com/corpnewt/GenSMBIOS) 生成 `MacBookPro16,2`
   的序列号、MLB、SystemUUID，填进 `config.plist` 的 `PlatformInfo → Generic`；ROM 填本机网卡 MAC。
   **不要使用别人的序列号。**

3. **BIOS 设置。** DVMT Pre-Allocated 设为 96 MB；关闭 Secure Boot。

4. **先用 U 盘启动。** 把 `EFI` 放到 U 盘的 FAT32 分区上。首次安装 macOS 时显卡还没有驱动，先在 boot-args
   里加 `-wex-noaccel`，装好系统后再去掉。

5. **安装 WhateverXe Graphics。** 进入 macOS 后按
   [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics) 的说明安装图形包（装进系统，不在 EFI 里）。
   SIP 已按它的要求设为 `0x803`。

6. 确认一切正常后，再考虑把 EFI 写入内置硬盘。作者把它放在内置硬盘上一个独立的 FAT32 分区里，没有改动 Windows 的 ESP。

### 启动参数

| 参数 | 作用 |
|---|---|
| `alcid=19 alcapplid=19` | AppleALC layout 19。只写 `alcid` 不够，AppleHDA 实际用的 layout 由 `alcapplid` 决定 |
| `-allow3d -disablegfxfirmware` | WhateverXe 要求：打开 3D 路径，不加载 GuC 固件 |
| `-wex-tgl-sdt-guard` | 本项目补丁：Tiger Lake 显示时序保护 |
| `-wex-no-panel-keep -wex-sink-rate-restore` | 本项目补丁：关屏允许面板断电，点亮时写回链路速率表 |
| `-noDC9 -nodisplaysleepDC6` | 关屏时不进 DC9 / DC6，保证能重新点亮 |
| `-v keepsyms=1 debug=0x100` | 调试用，稳定后可去掉 |

### ACPI

| SSDT | 作用 | 配套改名 |
|---|---|---|
| SSDT-RTCAWAC | 关 AWAC、启用传统 RTC | — |
| SSDT-PLUG | CPU 电源管理（`plugin-type`） | — |
| SSDT-EC | macOS 下让原有的 `H_EC` 保持启用，另加一个名为 `EC` 的占位设备 | `H_EC._STA → XSTA` |
| SSDT-PNLF | 背光，挂在 `\_SB.PC00.GFX0` | — |
| SSDT-PWRBEC | 电源键：`_REG` 时读一遍 EC RAM | `H_EC._REG → XREG` |
| SSDT-HIDDSTA-v2 | macOS 下打开 Intel HID 事件设备（调查期留下，不影响功能） | `HIDD._STA → XSTA` |

## 本项目的主要工作

- **显示**：在 WhateverXe 0.5.0 上做了 Tiger Lake 显示时序保护和面板速率恢复（[patches/WhateverXe](patches/WhateverXe)）。
- **电源键**：查明按键时 EC 不发 SCI 是因为 EC 地址空间没被访问过，`SSDT-PWRBEC` 补一次读取即可。
- **音频**：通过 Linux 反向验证，找到 ALC256 coef `0x10` bit9 是扬声器开关，写进 AppleALC layout 19。
- **触摸与手写笔**：VoodooI2C 2.9.1 加本机 I²C 控制器（`0xa0e88086&0xFFFCFFFF`）匹配，配合 Tiger Lake LP 的 VoodooGPIO。
- **USB**：逐口插拔实测，生成 USBToolBox 映射。

## 许可证

本仓库自己的代码、ACPI 补丁和文档使用 [BSD 3-Clause](LICENSE)。

第三方组件见 [THIRD_PARTY.md](THIRD_PARTY.md)。大部分第三方二进制不放进仓库，由脚本从上游下载。
仓库里只有三类第三方派生文件：WhateverXe.kext（BSD-3-Clause，补丁和原版权声明都在 `patches/WhateverXe/`）、
上游 CI 构建的 VoodooGPIO.kext（GPL-3.0，附许可证全文和对应源码版本），以及改过的 AppleALC / VoodooI2C 的 Info.plist。
WhateverXe Graphics 的图形驱动是 Apple / Intel 的二进制，请从其项目获取，本仓库不分发。

## 致谢

[Acidanthera](https://github.com/acidanthera)（OpenCore、Lilu、VirtualSMC、AppleALC 等）、
[b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe)、[VoodooI2C](https://github.com/VoodooI2C/VoodooI2C)、
[OpenIntelWireless](https://github.com/OpenIntelWireless)、[laobamac/itlwm](https://github.com/laobamac/itlwm)、
[USBToolBox](https://github.com/USBToolBox)、[1Revenger1/ECEnabler](https://github.com/1Revenger1/ECEnabler)、
[Dortania](https://github.com/dortania)。
