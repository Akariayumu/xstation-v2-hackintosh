# S0ixStandby 补丁（X-STATION V2）

本目录维护针对上游 [labrouss/S0ixStandby](https://github.com/labrouss/S0ixStandby) **v0.5.4**（commit `28069d3` / release `c52f172`）的 X-STATION V2 定制补丁 `s0ixstandby-0.5.4-xstation-v2.patch`（向下包含 `v1` 补丁），用于构建 `EFI/OC/Kexts/S0ixStandby.kext`。

## 1. 为什么需要 `-s0ixacnoecwake`（`v1`）

X-STATION V2 的嵌入式控制器 `H_EC` 与 ACPI 电源键 `\_SB.PWRB` 共用同一根 `GPE0_STS[110]`（`GPE 0x6E`）：

- 在 `SSDT-DEEPIDLE.dsl` 中为 `\_SB.PWRB` 补齐 `PRW0 = Package (2) { 0x6E, 0x04 }` 后，**纯电池模式**下短按实体电源键可以正常唤醒 S0ix Deep Idle。
- 但在**插电（AC）充电模式**下，`H_EC` 在充电电流变化或电池状态刷新时同样会拉高 `GPE 0x6E` 触发 SCI 查询，若 `GPE 0x6E` 处于 S0 唤醒使能态，会导致机器入睡后被充电脉冲提前唤醒。
- 逆向 `AppleACPIPlatformExpert::sleepPlatform()` / `updateWakeDevices()` 可知，macOS 只对位于内部 `fWakeDevices` 集合中的设备打开 `"acpi-wake-s0-gpe"`（`0x6E`），唤醒后调用 `_AcpiHwEnableAllRuntimeGpes()` 自动恢复全部运行态 GPE（因此日常亮屏下的电池/电源键查询不受影响）。

`-s0ixacnoecwake`（`v2` 智能充电期策略）：监听 `IOPMPowerSource` 的 `ExternalConnected`、`IsCharging`、`FullyCharged`、`CurrentCapacity/MaxCapacity` 与 `InstantAmperage/Amperage` 状态，**仅在电池真正处于充电过程（容量 `< 94%` 或存在正向充电电流）时**通过公开 KPI `IOACPIPlatformDevice::setSystemWakeCapabilityEnable(false)` 临时屏蔽 `PWRB` 的 `GPE 0x6E` S0 唤醒（保留 USB 键鼠唤醒，防止充电脉冲误亮屏）；**一旦充满进入零电流稳态（`on AC (charged)`）或拔掉电源切回电池模式**，自动调用 `setSystemWakeCapabilityEnable(true)` 恢复实体电源键短按唤醒与拔插充电器唤醒。全程不写任何 EC RAM。

## 2. 按需只读导出 Gen12 核显实时频率（`v2`）

在 Tiger Lake（Gen12 `Iris Xe`）上，macOS 用户态 `PerformanceStatistics` 与 `powermetrics` 均不导出核显实时频率。`s0ixstandby-0.5.4-xstation-v2.patch` 在保留 `v1` 全部行为的基础上，新增零轮询、零 Forcewake、纯只读的按需查询入口：

- 在 `setupGPU()` 成功映射 Gen12 `gpuMMIO` 时初始化 IORegistry 属性 `"GPU Current Freq MHz" = 0` 与 `"GPU Requested Freq MHz" = 0`（`uint32` `OSNumber`），供用户态监控工具被动探测能力。
- 重写 `S0ixStandby::setProperties(OSObject *properties)`：当用户态调用 `IORegistryEntrySetCFProperties(..., {"refreshGpuFreq": kCFBooleanTrue})`（免 `root`）时，在 `gpuFreqLock` 互斥保护与睡眠态守卫下：
  1. 只读检查 PCI `PMCSR`（处于 `D0`）与 `cmd != 0xFFFF && (cmd & kIOPCICommandMemorySpace)`（已使能），**绝不改写 PCI Command 寄存器**；
  2. 在常驻 Uncore 镜像区间（`0x40000..0x1bffff`，无需任何 Forcewake 且绝不唤醒 `RC6`）只读检查硬件核心状态 `GEN6_GT_CORE_STATUS`（`0x138060`）：若 `coreStatus == 0xFFFFFFFF` 或 `(coreStatus & 0x7) >= 3`（硬件处于 `RC6=3` / `RC7=4` 深度休眠），直接发布 `0 MHz`，**绝不触发任何 Forcewake 写入**；
  3. 当硬件处于活跃态（`(coreStatus & 0x7) < 3`）时，只读采样 Uncore 镜像 `GEN12_RPSTAT1`（`0x1381B4`，`cagf = (v >> 11) & 0x1FF`，`actualMHz = (cagf * 50 + 1) / 3`）；对于位于 `FORCEWAKE_GT` 电源域（`0x9800..0xafff`）的 `GEN6_RPNSWREQ`（`0x0A008`），仅当常驻寄存器 `FORCEWAKE_GT_ACK`（`0x130044`）已有活跃软件租约（`(gtAck & 0xFFFF) != 0`）时才只读采样（`req = (v >> 23) & 0x1FF`），否则当 `actualMHz > 0` 时安全回退 `reqMHz = actualMHz`，最终更新 `"GPU Current Freq MHz"` 与 `"GPU Requested Freq MHz"`。

## 常用启动参数

| 启动参数 | 来源 | 作用 |
|---|---|---|
| `-s0ixgpureset` | 上游 0.5.4 | 唤醒时对 Tiger Lake GT 执行硬件 `GEN11_GDRST`（约 125–155 µs），避免核显从深睡恢复后渲染引擎卡死 |
| `-s0ixrefclk` | 上游 0.5.4 | 睡眠期间允许关闭 PCIe 参考时钟，解除 PCH `SLP_S0` 阻塞 |
| `-s0ixd3` | 上游 0.5.4 | 入睡前将无驱动的 PCI 设备（ISH、TBT DMA、LPSS、DPTF 等）切入 `D3hot`，唤醒后恢复 `D0` |
| `-s0ixacnortc` | 上游 0.5.4 | 插电且无用户态显式 `pmset schedule` 定时任务时，抑制 macOS 自动维护型 RTC 唤醒 |
| `-s0ixacnoecwake` | 本项目补丁 | 插电快充期间临时屏蔽共享 `GPE 0x6E` S0 唤醒防充电脉冲误醒；充满稳态或电池模式下自动恢复电源键与拔插充电器唤醒 |
| `-s0ixdbg` | 上游 0.5.4 | 输出详细串口/内核调试日志（排障用，日常可省略） |

## 复现构建

在安装了 Xcode Command Line Tools 的 macOS 上执行：

```bash
git clone --branch main https://github.com/labrouss/S0ixStandby /tmp/S0ixStandby
git -C /tmp/S0ixStandby checkout 28069d3
git clone --depth 1 https://github.com/acidanthera/MacKernelSDK /tmp/S0ixStandby/MacKernelSDK
./patches/S0ixStandby/build.sh /tmp/S0ixStandby /tmp/S0ixStandby/MacKernelSDK
```

构建产物位于 `/tmp/S0ixStandby/build/Release/S0ixStandby.kext`。

## 许可证

上游 `S0ixStandby` 使用 BSD 3-Clause（原版权声明见 [`LICENSE`](LICENSE)，© 2026 The S0ixStandby Authors）。
本目录补丁与构建脚本同样按 BSD 3-Clause 发布。
