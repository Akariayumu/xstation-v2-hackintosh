# 版本历史

X-STATION V2（i7-1165G7 / Iris Xe `8086:9A49` / 3840×2400 eDP）OpenCore EFI 的逐版本记录。
日期均为 2026 年。早期（v0.1.0–v0.1.35）以 macOS Tahoe 26.7.1 为目标，v0.1.36 起改为 macOS Sequoia 15.8.1（24H32）。

“结果”一栏的写法：

- **成功**：实机验证通过并保留。
- **部分成功**：有进展，但仍有明显问题。
- **失败**：实机测试没有达到目的，已回退。
- **仅诊断**：只为收集日志或寄存器数据，不打算长期使用。
- **未上机 / 放弃**：只打包或准备，没有实际启动测试。

失败的版本也保留在表里。用同型号硬件排查问题时，这些记录可以省掉重复试错。

## 里程碑

- **v0.1.15**：越过 `EXITBS:START` 卡死和 Recovery 键鼠失灵，进入 Recovery 并装好 Tahoe 26.7.1。关键改动有三项：v0.1.11 按同 CPU 的 Yoga EFI 对齐 quirks，v0.1.14 改用无 MAT 的 Booter 组合，v0.1.15 加入 USBToolBox 临时枚举。
- **v0.1.33**：Tahoe 下 4K 内屏第一次正确显示，不再黑屏或花屏。根因是 AGDC 回调失败后显示时序没有初始化，导致 `SetupTimings` 等函数除以零。修法是在 WEX 补丁里写回已验证的 4K 时序，对应启动参数 `-wex-timing-repair`。
- **v0.1.34–v0.1.35**：Tahoe 打开加速后 Metal 设备能枚举，但 WindowServer 每 9 秒左右崩溃一次（`Xe compiler unsupported bitcode layout`）。离线定位到 WhateverXe 的着色器转换器处理不了 CoreAnimation 的一个着色器变体，本地修补没有成功，因此决定回退到 Sequoia。
- **B′ 基线（v0.1.36 的底包）**：把 v33 的时序修复移植到 Sequoia 后，Sequoia 15.8.1 上 4K 桌面和 Metal 2 加速都可用，WindowServer 稳定。
- **v0.1.36 / v0.1.40**：AX201 的 Wi-Fi（AirportItlwm）和蓝牙（IntelBluetoothFirmware 套件）可用。
- **v0.1.41**：触摸屏手指触控可用（VoodooI2C）。
- **v0.1.42**：解决关屏后无法重新点亮。关屏时跳过 DC9/DC6；面板断电后，在 WhateverXe 补丁里写回链路速率表 `0x115`。同时换用干净重写的 WhateverXe 0.5.0。
- **v0.1.44 / v0.1.45**：内置扬声器出声。ALC256 的 coef `0x10` bit9 是扬声器开关，清零的写入已固化进 AppleALC layout 19。
- **v0.1.50**：电源键可用。原因是 macOS 下 EC 地址空间没有被读过，EC 就不发电源键查询 `0x54`；修法是在 `_REG` 时补读一遍 EC。
- **v0.1.51 / v0.1.52**：手写笔可用（VoodooI2C 2.9.1）；触摸改用 Tiger Lake GPIO 中断，不再以 5 ms 间隔轮询。
- **v0.1.55**：实测定制 USB 映射。同一天把这一版写进内置硬盘的独立 FAT32 分区，可以拔掉 U 盘启动。

## 版本表（新 → 旧）

| 版本 | 日期 | 改动 | 原因 / 解决的问题 | 结果 |
|---|---|---|---|---|
| v0.1.55 | 10-29 | 用实测的 `UTBMap.kext` 替换 `UTBDefault`（临时全开） | 正式 USB 映射；BIOS 的 `_UPC/_PLD` 是公版模板，和实际接口不符 | 成功：3 个外部口和摄像头、读卡器、蓝牙都正常 |
| v0.1.54 | 10-29 | NVMe 和它所在根端口 RP09 加 `pci-aspm-default=02000000` | 继续降低关屏待机功耗（按 NVMeFix README 的建议） | 成功但无可测收益（固件可能已开 L1），保留 |
| v0.1.53 | 10-29 | 加 NVMeFix 1.1.3 | 第三方 NVMe（Kingston）在 macOS 下没有 APST，关屏待机偏高 | 成功：关屏整机（插座端）4.5 W 降到约 3.8 W |
| v0.1.52 | 10-29 | VoodooGPIO 换成上游 CI 构建（含 `VoodooGPIOTigerLakeLP`），`INT34C5` 改由它匹配 | v0.1.51 把 GPIO 当 Ice Lake，引脚 337 映射不到，触摸退回 5 ms 轮询（约 180 次/秒空读） | 成功：改为 GPIO 中断，空报告从 907 次/5 s 降到个位数 |
| v0.1.51 | 10-29 | VoodooI2C 套件 2.8 → 2.9.1，并在 Info.plist 补本机匹配（`0xa0e88086&0xFFFCFFFF`、`INT34C5`） | 2.8 只解析 TouchScreen 集合，笔数据（Pen report）被丢弃 | 成功：悬停、笔尖、侧键、橡皮擦可用；第一次刷入时漏打匹配补丁，触摸全部失效 |
| v0.1.50 | 10-28 | 加 `SSDT-PWRBEC`：`H_EC._REG` 改名为 `XREG` 并包装，在 Darwin 下读一遍 EC RAM 0x00–0xFF | v0.1.49 证明开机读一次 EC 就能让电源键工作 | 成功：电源键事件正常，Ctrl+电源 弹出关机框 |
| v0.1.49 | 10-28 | 探针改为每秒重试，读到一次 EC 后就停止 | v0.1.48 的单次读取太早，实际没有读到 | 仅诊断：开机读一次后，4 分钟后按键仍能触发 `_Q54`，不需要持续轮询 |
| v0.1.48 | 10-28 | EC RAM 探针改成只在开机读一次 | 区分“开机读一次即可”和“需要持续访问 EC” | 仅诊断（失败）：读取时机过早，等于没读，电源键无事件，反向证实读 EC 是开关 |
| v0.1.47 | 10-28 | 探针换成每 5 秒读一遍 EC RAM 0x00–0xFF（保留 `_Q54→XQ54`） | 与 Linux 下的 EC RAM 逐字节对比 | 仅诊断：意外发现开着这个探针时电源键就能用 |
| v0.1.46 | 10-28 | 加 `SSDT-BTNEPROBE` + DSDT `_Q54→XQ54` + 读数 kext，开机调用一次 `HIDD.BTNE` | 假设 EC 要先收到 intel-hid 的 BTNE 才会发电源键查询 | 失败：BTNE 已调用，但按键时 EC 仍不发 SCI，假设不成立 |
| v0.1.45 | 10-28 | 去掉 `alcverbs=1`，保留 v0.1.44 的 layout 修改 | `alcverbs` 只用于读写调试，日常不需要 | 成功：扬声器、耳机、麦克风正常 |
| v0.1.44 | 10-28 | AppleALC layout 19 的 ConfigData/WakeConfigData 末尾追加 `02050010 02040020`（coef 0x10 = 0x0020） | Linux 反向验证：coef 0x10 bit9 置位扬声器就哑，清零就恢复 | 成功：开机即出声，扬声器问题解决 |
| v0.1.43 | 10-28 | boot-args 加 `alcverbs=1`（只读） | 在 macOS 播放时读 ALC256 各节点状态，和 Linux 对比 | 仅诊断：得到对比数据，最终定位到 coef 0x10 |
| v0.1.42 | 10-28 | 换干净重写的 WhateverXe 0.5.0（候选 11），加 `-wex-tgl-sdt-guard -noDC9 -nodisplaysleepDC6 -wex-no-panel-keep -wex-sink-rate-restore` | 关屏后点亮黑屏：退出 DC9/DC6 后 AUX/PHY 没有恢复；面板断电后丢失链路速率表 `0x115=06`，回落到 8.1 Gbps | 成功：关屏点亮反复通过；DC9/DC6 只是绕开，没有修复。系统睡眠仍未验证 |
| （未编号） | 10-28 | WhateverXe 候选 8–10：开机快照 DPCD，尝试在训练前写回 D0、OUI 或速率表 | 查明面板断电后到底丢了哪个状态 | 仅诊断：候选 10 开机训练就失败；候选 11 只写回 `0x115` 后成功，即 v0.1.42 |
| （未编号） | 10-27 | WhateverXe 候选 5/6：关屏时让面板保持供电；再加 `-noDC9 -nodisplaysleepDC6` | 关屏后点亮时第一笔 AUX 就超时，链路训练返回 `0xe00002bc` | 成功（临时方案）：关屏点亮 11/11，后被 v0.1.42 的速率恢复取代 |
| （未编号） | 10-27 | `SSDT-PNLF` 从 ACPI 根移到 `\_SB.PC00.GFX0` | 亮度滑块无效，点亮后背光占空比为 0；本机 PCI 根名是 `PC00` 不是 `PCI0`（第一次写成 `PCI0`，没生效） | 成功：出现 `AppleBacklightDisplay`，亮度可调 |
| （未编号） | 10-27 | WhateverXe 改用上游 0.5.0 源码重写的最小补丁（`-wex-tgl-sdt-guard`：时序保护 + PG2/PG3 电源井轮询），去掉 B′ 参数 | B′ 的 0.4.0 间歇性 panic（`setupPipeWatermarks` 除零，2 次启动 1 次）；关屏后出现 `PG2 power well fuse distribution failed` panic；B′ 二进制来源难以复核 | 成功：开机稳定，每次启动保护都修复了 5 次无效时序 |
| （未编号） | 10-26～27 | 电源键探针：`SSDT-PWRB43`（`_L43`→`Notify(PWRB)`）、暴露 HIDD（INTC1051）+ 日志 kext、全部 `_Q00`–`_QFF` 计数探针 | macOS 下电源键完全没有事件 | 失败 / 仅诊断：Linux 下是 EC Query `0x54`→`_Q54`，macOS 下 `_Q54` 从未进入；HIDD 路线无关 |
| （未编号） | 10-26 | 加 VoodooPS2Controller 2.3.7（只留 Keyboard 插件，不加 Mouse/Trackpad/VoodooInput） | Linux `evtest` 证实音量键是 PS/2 扫描码，不走 Intel HID | 成功：音量键可用 |
| （未编号） | 10-26 | 加 SMCBatteryManager 1.3.8，再加 ECEnabler 1.0.6 | 没有电池信息；EC 的 `_BIF/_BST` 返回 `AE_BAD_PARAMETER` | 成功：电量与充电状态正常（只加 SMCBatteryManager 时无效） |
| （未编号） | 10-25～26 | AppleALC 依次试 layout 5/11/13/21、19、20、69、76，并加 `alcapplid=N` | v0.1.40 扬声器无声；`alcid` 只改 `alc-layout-id`，AppleHDA 实际用的 layout 由 `alcapplid` 决定（默认 7） | 失败：20/69/76 没有输出设备；19 有输出但扬声器无声，暂留 19 |
| v0.1.41 | 10-25 | 加 Yoga 13ITL5 EFI 里的 VoodooI2C 2.8 套件（GPIO 1.1 匹配 `INT34C5`，I2C 掩码覆盖 `a0e9`） | Wacom 触摸屏走 I²C HID，原先没有驱动 | 成功（10-26 验证）：手指触摸正常；实际部署在 layout 19 音频配置之上；笔未识别 |
| v0.1.40 | 10-25 | Wi-Fi + 蓝牙（IntelBluetoothFirmware/IntelBTPatcher 2.4.0、BlueToolFixup 2.7.2 + 两个蓝牙 NVRAM 键）+ AppleALC 1.9.8 `alcid=5` | 应要求一次性测试三项 | 部分成功：Wi-Fi、蓝牙可用；声卡已枚举、耳机有声，但扬声器无声 |
| v0.1.39 | 10-25 | Wi-Fi + Intel 蓝牙三件套 | 蓝牙显示 `BCM_4350C2 / Address NULL / Off`，Intel 固件没加载 | 未上机：被 v0.1.40 合并包取代 |
| v0.1.38 | 10-25 | Wi-Fi + AppleALC `alcid=5` | 保留已能用的 Wi-Fi，只新增音频 | 未上机：被 v0.1.40 合并包取代 |
| v0.1.37 | 10-25 | 只加 AppleALC 1.9.8，`alcid=5` | 音频设备列表为空，`AppleHDAController` 没有 codec | 未上机：被 v0.1.38/v0.1.40 取代 |
| v0.1.36 | 10-25 | B′ 加速基线 + AirportItlwm 2.4.0-alpha（laobamac 分支，Min/MaxKernel 24.6.0） | Sequoia 下 AX201 没有 Wi-Fi 接口 | 成功：Wi-Fi 菜单可用，WPA2 能连接 |
| （B′ 基线，未编号） | 10-25 | WhateverXe 0.4.0 + v33 时序修复，新增 Sequoia 准入（24.6 + `-wex-sequoia-timing-test`）；系统装 WhateverXe Graphics 0.2.0；先 `-wex-noaccel`，再去掉 | Tahoe 加速走不通；同 GPU 的其他机器在 Sequoia 上有成功报告 | 成功：Sequoia 15.8.1 下 4K 桌面 + Metal 2，WindowServer 稳定 |
| （Sequoia 测试，未编号） | 10-25 | v33 EFI 改用官方 WhateverXe 0.5.0，MinKernel 改为 24 | 第一次尝试 Sequoia | 失败：WindowServer 在 `SetupTimings` 除零 panic（官方版没有时序修复） |
| v0.1.35 | 10-25 | v0.1.34 + `csr-active-config=0x807`（允许调试器附加） | 用 LLDB 抓 WindowServer 失败的 pipeline | 仅诊断：抓到部分数据，但调试过程中多次冻机；根因随后离线定位到 WhateverXe 着色器转换器 |
| v0.1.34 | 10-25 | v0.1.33 去掉 `-wex-noaccel`；系统侧改装 Graphics 0.2.0 | 打开 GPU 加速 | 失败：Metal 2 设备存在，基础计算/渲染测试通过，但 WindowServer 每 9 秒左右崩溃（`Xe compiler unsupported bitcode layout`） |
| v0.1.33 | 10-25 | 新增 `-wex-timing-repair`：保存验证过的 4K 时序，在 `setupClocks/SetupTimings` 拿到无效时序时写回 | AGDC link-modeset 回调失败时输出时序没有初始化，下游拿到时钟 0 | 成功：Tahoe 下内屏正确显示（3008×1880，UI 1504×940），无加速 |
| v0.1.32 | 10-25 | 只改 boot-args：去掉 capture，打开 `-wex-setup-guard` | 让合法的 4K 时序进入原生 `SetupTimings` | 失败：仍回退到 1280×800，`SetupTimings` 拿到的时钟仍为 0（据此找到 v0.1.33 的根因） |
| v0.1.31 | 10-25 | `-wex-watermark-guard`：`computeWatermarkValue` 遇时钟 0 返回 0，并记录 `setupClocks` | v0.1.29/30 的 cursor watermark 除零 panic | 部分成功：第一次进桌面，但画面横纹且竖切成 4 份（回退到 1280×800 却用了 4K 时序） |
| v0.1.30 | 10-25 | `-wex-setup-capture`：只记录 `SetupTimings` 输入，不调用原函数 | 不让系统崩溃，先拿到 4K 时序和 lane 数据 | 失败：cursor watermark 除零发生得更早，没拿到数据 |
| v0.1.29（guard-on） | 10-24 | 同 WEX，加 `-wex-setup-guard`，放行 4K | 测试 `SetupTimings` 入口保护 | 失败：`AppleIntelPlane::computeWatermarkValue` 除零 panic（时钟为 0） |
| v0.1.29（opt-in 安全版） | 10-24 | 新增 `SetupTimings` 调用点保护，默认关闭 | 先确认不带参数时行为与 v0.1.28 相同 | 仅诊断：如预期，4K 被拦住，SSH 正常 |
| v0.1.28 | 10-24 | 注入只含原生 3840×2400@59.94 一条时序的 EDID，并加 lane 计算记录 | v0.1.27 证明不能用低分辨率填满面板 | 仅诊断：`setDisplayMode` 处的 4K 时序合法，但被安全闸拦住，没有桌面 |
| v0.1.27 | 10-24 | 用 `AAPL00,override-no-connect` 注入 1920×1200 EDID，加 4K 安全闸 | 避开 4K 时序除零，先求有画面 | 失败：只在屏幕左侧约 1/4 显示，上下重复，放弃低分辨率路线 |
| v0.1.26 | 10-24 | 在 v0.1.23 上增加时序和 lane 选择的记录 | 查清 4K 为什么只用 1 条 lane | 失败：启动约 19 秒后 `SetupTimings+0x159` 除零 panic，说明 v0.1.23 的保护并不可靠 |
| v0.1.25 / 25b / 25c / 25d | 10-24 | 换官方 WhateverXe 0.5.0，分别为：加速版 / `-wex-noaccel` / DEBUG + `-wexdbg` / Lilu DEBUG + `liludump` | 0.5.0 新增 TGL HBR3 电气参数，当时怀疑内屏 4 条 lane 只锁住 1 条 | 失败：4 个变体都在启动早期卡死，没有日志落盘；当时在 Tahoe 上放弃 0.5.0 |
| v0.1.24 | 10-24 | v0.1.23 去掉 `-wex-noaccel` | 怀疑关掉加速器阻断了 Tahoe 的显示呈现链 | 失败：加速器已注册，但 WindowServer 报 `Display not ready` / GPU Busy，看门狗 panic |
| v0.1.23 | 10-24 | WhateverXe 0.4.0 自编译版：`setDisplayMode` 前校验时序，非法就拒绝；保留 `-wex-noaccel` | v0.1.20/21 的 5 次 panic 都是 WindowServer→`SetupTimings+0x159` 整数除零 | 部分成功：不再 panic、SSH 可用，但内屏黑屏有背光（`IOPresentment` 0x815） |
| v0.1.22 | 10-24 | 改用 NootedGreen（XPS 13 Plus 项目的 TGL 方案）的独立 EFI | 作为 WhateverXe 的替代路线 | 放弃：只完成打包和目标机编译，没有安装和启动 |
| v0.1.21 | 10-24 | v0.1.20 + `-wex-noaccel` | 区分卡在显示还是卡在加速器 | 失败：framebuffer 初始化返回成功，但仍无桌面（事后从日志确认 WindowServer 除零 panic） |
| v0.1.20 | 10-24 | Lilu/WhateverXe 换回同版本 RELEASE，去掉调试日志参数 | v0.1.19 的 Double fault 出在 Lilu DEBUG 日志路径 | 失败：越过了 Double fault，加速器启动返回，但无桌面 |
| v0.1.19 | 10-24 | 删除 60 MB DVMT 的三项补丁，BIOS DVMT 改为 96 MB | 去掉 DVMT 变量（WhateverXe 推荐 96 MB 以上） | 失败：Double fault panic（Lilu DEBUG 日志路径） |
| v0.1.18 | 10-24 | v0.1.17 + `-wex-noaccel` | 只测 framebuffer | 未上机：被 v0.1.19 / v0.1.21 取代 |
| v0.1.17 | 10-24 | Lilu 1.7.2 / WhateverXe 0.4.0 换 DEBUG 版，加 `-wexdbg -liludbgall liludump=60` | 装 Graphics 包后启动停住，需要日志 | 失败：第二次安装 Graphics 后仍停在 verbose 画面，没拿到有效日志 |
| v0.1.16 | 10-24 | 加 AMFIPass 1.4.1 + WhateverXe 0.4.0、SSDT-PNLF、`-allow3d -disablegfxfirmware`、SIP `0x803`、两项 60 MB DVMT 属性 | 开始核显适配（此前只有 3 MB 显存的基础显示） | 部分成功：两个 kext 加载正常；装 Graphics 0.1.0 后启动停住，在 Recovery 恢复了 AuxKC |
| v0.1.15 | 10-23 | 加 USBToolBox 1.2.0 + UTBDefault（临时枚举） | Recovery 的键鼠连接页上外接键鼠没有反应 | 成功：进入 Recovery 主界面，随后装好 Tahoe 26.7.1 并进入桌面 |
| v0.1.14 | 10-23 | Booter 改为无 MAT 组合（`EnableWriteUnprotector=true`，`RebuildAppleMemoryMap`/`SyncRuntimePermissions=false`） | 固件报告 MAT=0，v0.1.11 用的是 MAT 组合 | 部分成功：越过内核重启，进入图形界面的键鼠连接页，但键鼠无效 |
| v0.1.13 | 10-23 | OpenCore 输出分辨率改为 1024×768，UIScale 1 | 屏幕字太小，报错看不清 | 仅诊断：看到 SSDT-EC 已枚举，之后在 CPU 枚举附近重启，没有明确 panic |
| v0.1.12 | 10-23 | 加 SSDT-EC，并把 `H_EC._STA` 精确改名为 `XSTA` | v0.1.11 panic 前出现 `ECF2 has no handler` 等 EC 报错 | 失败：ACPI 报错后重启；EC 补丁保留进后续版本 |
| v0.1.11 | 10-23 | 按同 CPU 的 Yoga 13ITL5 EFI 成组对齐 8 项 Booter/Kernel quirks（含 `AppleXcpmExtraMsrs`） | v0.1.2–v0.1.10 都卡在 `EXITBS:START` | 部分成功：第一次进入内核，随后 page fault panic |
| v0.1.10 | 10-23 | v0.1.9 + `RebuildAppleMemoryMap=true` | 内存映射对照 | 记录不全：没有实机结果，直接进入 v0.1.11 |
| v0.1.9 | 10-23 | 恢复完整基线，加 `ForceExitBootServices=true` | v0.1.8 最小版缺少必要配置，不是有效对照 | 失败：仍停在 `EXITBS:START` |
| v0.1.8 | 10-23 | 最小版 + `ForceExitBootServices=true` | 怀疑固件的 ExitBootServices 调用失败 | 失败：停在同一位置 |
| v0.1.7 | 10-23 | 最小版：去掉 SSDT、kext、内核补丁和 CPUID 伪装 | 判断是否连最基本的交接都失败 | 失败：同样卡住（但这个对照缺少必要配置，结论有限） |
| v0.1.6 | 10-23 | 恢复 v0.1.2 的 Booter，加 CPUID 伪装为 Ice Lake-U | 怀疑内核不认 Tiger Lake | 失败：伪装已生效，仍卡住；同时发现 BIOS 开着 CFG Lock |
| v0.1.5 | 10-23 | `SetupVirtualMap=true` | 怀疑卡在 `SetVirtualAddressMap` | 失败：基本排除 Booter 内存映射方向 |
| v0.1.4 | 10-23 | `ProtectUefiServices=false` | 单变量排查 | 失败 |
| v0.1.3 | 10-23 | `DevirtualiseMmio=false` | 怀疑 MMIO 去虚拟化拿掉了固件仍需要的区域 | 失败：排除 `DevirtualiseMmio` |
| v0.1.2 | 10-23 | Booter 改为无 MAT 组合 | 固件不支持 MAT（`MAT support is 0`） | 失败：仍停在 `EXITBS:START` |
| v0.1.1 | 10-23 | 加 ResetNvramEntry，放入 Recovery 26.6.2 镜像，`HideAuxiliary=false` | 准备从 U 盘启动 Recovery | 失败：选 Recovery 后停在 `EXITBS:START` |
| v0.1.0 | 10-23 | 初始 EFI：OpenCore 1.0.8、Lilu 1.7.2、VirtualSMC 1.3.8、SSDT-RTCAWAC、SSDT-PLUG，SMBIOS MacBookPro16,2 | 建立基础 EFI（AWAC/RTC 必须修） | 成功：U 盘能进 OpenCore 菜单，回退 Windows 正常 |

## 备注

- “未编号”指直接在 U 盘上修改、没有单独打包编号的状态，大多发生在 v0.1.41 和 v0.1.42 之间。v0.1.42 是这些修改整理后的第一个正式包。
- v0.1.42 里仍留着当时的诊断组件（S1 HIDD 日志 kext、`SSDT-HIDDSTA-v2`），它们不影响功能。电源键诊断用的探针在 v0.1.50 里全部去掉了。
- 系统睡眠一直禁用（`pmset SleepDisabled=1`），关屏可以代替睡眠。动态壁纸与远程桌面截屏同时运行时出现过 GPU 反复重启后 panic，根因尚未定位，见 README 的“已知问题”。
