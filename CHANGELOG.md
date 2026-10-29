# 版本历史

只记录有实际突破的版本，每条写明当时卡在哪里、解决思路和具体改动。中间试错、纯诊断和没有带来收益的版本都省略了，
关键的排查过程放在“思路”里。日期均为 2026 年。

早期以 macOS Tahoe 26.7.1 为目标，B′ 基线起改为 macOS Sequoia 15.8.1（24H32）。
“未编号”表示直接在 U 盘上修改、没有单独打包编号的改动，后来都合并进了 v0.1.42。

---

## v0.1.55（10-29）USB 定制映射，内置硬盘启动

- **问题**：一直用 USBToolBox 的临时全开映射；BIOS 里的 `_UPC/_PLD` 是公版模板，和实际接口对不上。
- **思路**：不信 BIOS 描述，逐个接口插拔 USB 2/3 设备，用 `tools/usb/usbwatch.sh` 记下每次变化的端口，按实测结果生成映射。
- **改动**：用实测的 `UTBMap.kext` 替换 `UTBDefault`。同一天把 EFI 写进内置硬盘上独立的 FAT32 分区，不再依赖 U 盘。
- **结果**：3 个外部接口、摄像头、读卡器、蓝牙都正常。

## v0.1.53（10-29）关屏功耗下降

- **问题**：关屏待机时整机功耗偏高。
- **思路**：这块第三方 NVMe（Kingston）在 macOS 下没有启用 APST 自主电源状态切换，硬盘一直处于高功耗状态。
- **改动**：加 NVMeFix 1.1.3。
- **结果**：关屏整机功耗（插座端）从 4.5 W 降到约 3.8 W。

## v0.1.52（10-29）触摸改用 GPIO 中断

- **问题**：触摸能用，但驱动退回 5 ms 轮询，每秒约 180 次空读，白白耗电。
- **思路**：日志显示 VoodooGPIO 把本机 GPIO 控制器当成 Ice Lake，触摸中断引脚 337 映射不到，所以只能轮询。
  上游 VoodooGPIO 的新提交已经加入 Tiger Lake LP 支持，只是还没有正式发布。
- **改动**：VoodooGPIO 换成上游 CI 构建（含 `VoodooGPIOTigerLakeLP`），`INT34C5` 交给它匹配。
- **结果**：改为中断驱动，空报告从 5 秒 907 次降到个位数。

## v0.1.51（10-29）手写笔可用

- **问题**：手指触摸正常，笔完全没反应。
- **思路**：抓取原始 HID 报告（`tools/pen/`），发现笔的数据其实一直在上报（Pen report 20）；但 VoodooI2C 2.8 只解析 TouchScreen 集合，Pen 报告被丢弃了。2.9.1 支持 Pen 集合。
- **改动**：VoodooI2C 套件从 2.8 升到 2.9.1，并在 Info.plist 补上本机 I²C 控制器匹配 `0xa0e88086&0xFFFCFFFF`。
- **结果**：悬停、笔尖、侧键、橡皮擦都可用。
- **注意**：漏掉这项匹配时，触摸会全部失效。

## v0.1.50（10-28）电源键可用

- **问题**：macOS 下按电源键没有任何事件。
- **思路**：
  1. Linux 下电源键走 EC Query `0x54` → `_Q54`；macOS 下计数探针显示 `_Q54` 从来没被调用，说明 EC 根本没发 SCI。
  2. 先怀疑 EC 要等 intel-hid 的 BTNE 调用，试过后被否定（v0.1.46）。
  3. 加了一个每 5 秒读一遍 EC RAM 的探针，本来是想和 Linux 对比数据（v0.1.47），结果发现开着这个探针时电源键就能用。
  4. 再缩小范围（v0.1.48–v0.1.49）：开机后读一次就够，不需要持续访问。

  结论：EC 地址空间要先被访问过一次，EC 才会开始发电源键查询。
- **改动**：加 `SSDT-PWRBEC`，把 `H_EC._REG` 改名为 `XREG` 并包一层，在 Darwin 下读一遍 EC RAM `0x00–0xFF`。
- **结果**：电源键事件正常，Ctrl+电源键会弹出关机框。短按弹面板需要配合 `tools/powerkey`。

## v0.1.44（10-28）内置扬声器出声

- **问题**：耳机和麦克风正常，扬声器无声。换遍了 layout 都没用。
- **思路**：
  1. 先查明 `alcid` 只改 `alc-layout-id`，AppleHDA 实际用哪个 layout 由 `alcapplid` 决定，所以两个都要设。
  2. 换 layout 解决不了，就在 Linux 下逐位改 ALC256 的厂商 coef，反向找扬声器开关：coef `0x10` 的 bit9 置位时扬声器就哑，清零就恢复。
  3. 再在 macOS 下用 `alcverbs` 读取（v0.1.43），确认这一位是置位的。
- **改动**：AppleALC layout 19 的 `ConfigData` 和 `WakeConfigData` 末尾追加 `02050010 02040020`（coef `0x10` 写 `0x0020`），boot-args 设 `alcid=19 alcapplid=19`。
- **结果**：开机即可出声，扬声器、耳机、麦克风都正常。

## v0.1.42（10-28）关屏后可以重新点亮

- **问题**：关屏后再点亮一直黑屏，第一笔 AUX 就超时，链路训练失败。
- **思路**：分两层。
  - 第一层：退出 DC9/DC6 低功耗状态后 AUX/PHY 没有恢复，直接不让它进 DC9/DC6。
  - 第二层：面板断电后会丢掉 BIOS 设置的链路速率表 DPCD `0x115=06`（5.4 Gbps），源端改按 8.1 Gbps 训练就失败。
    候选 8–10 分别试了写回 D0、OUI、速率选择，都不对；只写回 `0x115` 的候选 11 成功。
- **改动**：WhateverXe 补丁加 `-wex-sink-rate-restore`：开机快照记下 `0x115`，每次训练前写回。boot-args 加 `-noDC9 -nodisplaysleepDC6 -wex-no-panel-keep -wex-sink-rate-restore`。
- **结果**：反复关屏点亮都正常。
- **注意**：DC9/DC6 只是绕开，退出流程本身没有修好。系统睡眠仍未解决。

## （未编号，10-27）WhateverXe 改为基于 0.5.0 源码的最小补丁

- **问题**：B′ 基线用的 0.4.0 补丁版有两个 panic：开机约一半概率在 `setupPipeWatermarks` 除零；关屏后出现 `PG2 power well fuse distribution failed`。另外这份二进制的来源也难以复核。
- **思路**：不在旧二进制上继续叠补丁。从上游 0.5.0 的源码重写一个尽量小的补丁，只保留两项：时序保护，以及 PG2/PG3 电源井上电时轮询等待。每一处改动都能在源码里对上。
- **改动**：`-wex-tgl-sdt-guard`，补丁见 `patches/WhateverXe/`。
- **结果**：开机稳定，每次启动这个保护都修复了 5 次无效时序。

## （未编号，10-27）亮度可调

- **问题**：亮度滑块无效，点亮后背光占空比为 0。
- **思路**：网上常见的 SSDT-PNLF 挂在 `PCI0` 下，但本机的 PCI 根叫 `PC00`，设备挂错位置就不生效。
- **改动**：`SSDT-PNLF` 移到 `\_SB.PC00.GFX0`。
- **结果**：出现 `AppleBacklightDisplay`，亮度可调。

## （未编号，10-26）电池信息

- **问题**：系统里没有电池信息。
- **思路**：只加 SMCBatteryManager 不够，因为 EC 的 `_BIF/_BST` 在 macOS 下返回 `AE_BAD_PARAMETER`。原因是这台机器的 EC 字段宽度超过 8 位，macOS 读不了，需要 ECEnabler 让它能读多字节字段。
- **改动**：加 SMCBatteryManager 1.3.8 和 ECEnabler 1.0.6。
- **结果**：电量和充电状态正常。

## （未编号，10-26）音量键

- **问题**：音量键无效。
- **思路**：在 Linux 下用 `evtest` 确认，音量键发的是 PS/2 扫描码，不经过 Intel HID。
- **改动**：加 VoodooPS2Controller 2.3.7，只保留 Keyboard 插件，不加 Mouse、Trackpad 和 VoodooInput。
- **结果**：音量键可用。

## v0.1.41（10-25）触摸屏可用

- **问题**：触摸屏没有反应。
- **思路**：Linux 采集显示触摸屏是 Wacom 的 I²C HID 设备（`WCOM4815`），不是 USB。同 CPU 的 Yoga 13ITL5 EFI 里有现成的 VoodooI2C 组合，它的 GPIO 匹配了 `INT34C5`，I²C 掩码也覆盖了本机控制器。
- **改动**：加 VoodooI2C 2.8 套件。
- **结果**：手指触摸正常；笔要到 v0.1.51 才可用。

## v0.1.40（10-25）蓝牙可用

- **问题**：蓝牙面板显示 `BCM_4350C2 / Address NULL / Off`。
- **思路**：这表示 Intel 蓝牙固件没加载，系统把它当成了博通芯片的空壳。
- **改动**：加 IntelBluetoothFirmware 和 IntelBTPatcher 2.4.0、BlueToolFixup 2.7.2，以及两个蓝牙 NVRAM 键。同一版也加了 AppleALC（扬声器到 v0.1.44 才解决）。
- **结果**：蓝牙可用；声卡已枚举，耳机有声。

## v0.1.36（10-25）Wi-Fi 可用

- **问题**：Sequoia 下 AX201 没有 Wi-Fi 接口。
- **思路**：选用社区 fork laobamac/itlwm 的 Sequoia 版 AirportItlwm，它能用原生 Wi-Fi 菜单。
- **改动**：加 AirportItlwm 2.4.0-alpha（Sequoia 版）。
- **结果**：Wi-Fi 菜单可用，WPA2 能连接。

## B′ 基线（10-25）Sequoia 下 4K + Metal 2 加速

- **问题**：Tahoe 打开加速后，WindowServer 每 9 秒崩溃一次（`Xe compiler unsupported bitcode layout`）。离线分析发现，WhateverXe 的着色器转换器处理不了 CoreAnimation 的一个着色器变体，本地修补没有成功。
- **思路**：同款 GPU 在 Sequoia 上有成功报告，于是回退到 Sequoia。直接用官方 0.5.0 在 Sequoia 上同样会除零 panic，说明 v0.1.33 的时序修复还需要移植过去。
- **改动**：
  - WhateverXe 0.4.0 + v0.1.33 的时序修复，加上 Sequoia 准入（24.6 + `-wex-sequoia-timing-test`）。
  - 系统里安装 WhateverXe Graphics 0.2.0。
  - 先用 `-wex-noaccel` 进系统，确认正常后再去掉。
- **结果**：Sequoia 15.8.1 下 4K 桌面和 Metal 2 加速都可用，WindowServer 稳定。

## v0.1.33（10-25）内屏第一次正确显示

- **问题**：装上 WhateverXe 后，WindowServer 在 `SetupTimings+0x159`、`computeWatermarkValue` 等处反复除零 panic。
- **思路**：
  1. 从 panic 栈看，5 次 panic 都是除零。先在 `setDisplayMode` 前校验时序（v0.1.23），panic 消失了，但屏幕是黑的。
  2. 接着只记录、不执行，抓 `SetupTimings` 的输入（v0.1.26–v0.1.32），发现时钟一直是 0。
  3. 往上追，是 AGDC 的 link-modeset 回调失败了，输出时序根本没有初始化。
  4. 既然已经拿到一份验证过的 4K 时序，就在下游遇到无效时序时把它写回去。

  试过用低分辨率 EDID 绕开（v0.1.27），只显示 1/4 屏，放弃了。
- **改动**：WhateverXe 补丁加 `-wex-timing-repair`。
- **结果**：Tahoe 下内屏正确显示，但没有加速。

## v0.1.15（10-23）进入 Recovery，装好 macOS

- **问题**：已经能进图形界面，但 Recovery 的键鼠连接页上，外接键鼠都没有反应。
- **思路**：没有 USB 映射时，macOS 只会启用一部分端口，键鼠所在的端口可能没被启用。先用 USBToolBox 把所有端口临时打开。
- **改动**：加 USBToolBox 1.2.0 和 `UTBDefault`（临时全开映射）。
- **结果**：进入 Recovery，装好 Tahoe 26.7.1 并进入桌面。

## v0.1.14（10-23）越过内核重启，进入图形界面

- **问题**：进入内核后很快 page fault 或重启。
- **思路**：固件日志报告 `MAT support is 0`，但当时 Booter 用的还是依赖 MAT 的那组设置，改成不依赖 MAT 的组合。
- **改动**：`EnableWriteUnprotector=true`，`RebuildAppleMemoryMap=false`，`SyncRuntimePermissions=false`。
- **结果**：越过内核重启，进入图形界面。

## v0.1.11（10-23）第一次进入内核

- **问题**：v0.1.1 到 v0.1.10 都停在 `EXITBS:START`。逐项改过 `DevirtualiseMmio`、`ProtectUefiServices`、`SetupVirtualMap`、`ForceExitBootServices`，也试过伪装 CPUID，都没用。
- **思路**：单个变量排查一直没有进展，于是换了办法：拿同 CPU 的 Yoga 13ITL5 已经能用的 EFI 做对照，把不一致的 quirks 一次全部对齐。
- **改动**：成组对齐 8 项 Booter 和 Kernel quirks，包括 `AppleXcpmExtraMsrs`。
- **结果**：第一次越过 `EXITBS:START` 进入内核。之后还有 page fault，在 v0.1.12–v0.1.14 解决：v0.1.12 加了 SSDT-EC，v0.1.14 改了 Booter。

## v0.1.0（10-23）起点

- **改动**：
  - 基础 EFI：OpenCore 1.0.8、Lilu 1.7.2、VirtualSMC 1.3.8，SMBIOS 设为 `MacBookPro16,2`。
  - `SSDT-RTCAWAC`：Tiger Lake 默认用 AWAC 时钟，macOS 需要传统 RTC。
  - `SSDT-PLUG`：CPU 电源管理。
- **结果**：U 盘能进 OpenCore 菜单，回到 Windows 也正常。
