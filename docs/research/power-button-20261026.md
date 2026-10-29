# X-STATION V2 电源键 / 指纹进展

日期：2026-10-26  
目标：macOS Sequoia 15.8.1，X-STATION V2  
状态：电源键尚未工作；音量键已由 VoodooPS2 解决；指纹尚未支持。

## 一句话结论

实体电源键在 Linux 下明确走 **PWRB/PNP0C0C + EC Query(0x54)**，不是 HIDD 的
0xCE/0xCF。macOS 下 AppleACPIEC 已附着、EC 命令数会增长，但 PWRB/AppleACPIButton
没有观察到通知，Control+Power 也没有关机对话框。下一步必须做受控 ACPI 探针，
确认 macOS 是否执行 `_Q54`。

## 已确认事实

### 1. Linux 下电源键路径

`evtest` 监听 `event1 Power Button`，短按得到 `KEY_POWER`。

同时打开 `drivers/acpi/ec.c` dynamic debug 后，按下电源键出现：

```text
EC_SC(W) = 0x84
EC_DATA(R) = 0x54
Query(0x54) scheduled
Query(0x54) started
Query(0x54) stopped
```

GPE6E/SCI 在同一窗口增加 142。结论：

```text
电源键 -> EC Query(0x54) -> DSDT _Q54 -> Notify(PWRB,0x80) -> KEY_POWER
```

### 2. DSDT 中的相关路径

- `_Q54`：位于 `\_SB.PC00.LPCB.H_EC`，内容为
  `If (CondRefOf(\_SB.PWRB)) Notify(PWRB,0x80)`。
- `_QD5/_QD6`：`Notify(HIDD,0xCE/0xCF)`，是 HIDD 硬件特定通知；Linux 实测与
  本次电源键无关。
- 指纹传感器：`\_SB.PC00.SPI1.FPNT`，不是 PWRB；`_HID` 由 NVS `FPTT` 选择
  FPC/Goodix/ELAN/CS 等型号，详见 `acpi/dsdt-excerpts.txt`。

### 3. macOS 观察

- `AppleACPIEC` 已附着到 H_EC，`SCIActive=Yes`。
- `CommandCount` 随按键/轮询增长：965 -> 980 -> 1010 -> 1040。
- PWRB 下 `AppleACPIButton` 的 `NotificationCount` 始终为 0。
- `Control+Power` 没有弹出关机/重启/睡眠对话框。
- 用户态 `kIOGeneralInterest` 在 H_EC、PWRB、HIDD 上均无回调。
- S1 HIDD kext 的 `S1NotifyCount/S1InterestCount/S1MessageCount` 均为 0。
- 没有发生睡眠；`caffeinate` 曾保持运行。

这些结果只能说明“没有观测到 PWRB notify”，还不能证明 macOS 没执行 `_Q54`；
EC 命令数也可能包含电池轮询。

### 4. 已排除/不应继续的方向

- 不要再把电源键归因于 HIDD `0xCE/0xCF`；Linux 已证明不是。
- 不要再靠 S1 HIDD 的 `message()`/general interest 猜。
- 不要盲加 SSDT-PWRB：PWRB 已存在，`_STA=15`。
- 不要在未确认 `_Q54` 执行前直接写自定义 PWRB kext；Notify 未送达时它收不到事件。

## 下一步：受控 ACPI 探针（尚未执行）

目标：确认 macOS 是否执行 `_Q54`。

1. 备份 U 盘当前配置。
2. 用 OpenCore ACPI Patch 将 DSDT 的 `_Q54` 重命名为 `XQ54`。
3. 增加 SSDT：

   ```asl
   External (\_SB.PC00.LPCB.H_EC.XQ54, MethodObj)
   External (\_SB.HIDD, DeviceObj)
   Scope (\_SB.PC00.LPCB.H_EC)
   {
       Method (_Q54, 0, NotSerialized)
       {
           XQ54()
           Notify (\_SB.HIDD, 0x55)
       }
   }
   ```

4. 启动后用 S1 HIDD 观察 `S1NotifyCount`/`S1LastNotify`：
   - 出现 `0x55`：`_Q54` 执行，问题在 PWRB/AppleACPIButton 下游；
   - 没有 `0x55`：只能证明未观测到标记；必须先确认补丁加载、方法执行和通知接收通道，不能直接断言 macOS 未执行 `_Q54`。
5. 任何 ACPI 改动前必须有 U 盘备份、OC 配置备份和回退方案；不写内部 ESP。

## 文件索引

- `logs/linux-ec-query-power.txt`：Linux EC query/电源按键日志。
- `logs/linux-evtest-all.txt`：Linux event1/3/4/7 evtest 输出。
- `logs/macos-observation.txt`：macOS EC/observer 统计摘要。
- `acpi/dsdt-excerpts.txt`：_Q54、_QD5/_QD6、FPNT 指纹片段。
- `source/xshidd/`：S1 HIDD 日志-only kext 源码、Info.plist、构建脚本。
- `source/hidd_observer.py`：用户态 kIOGeneralInterest observer。
- `source/hidd_observer-README.md`：observer 用法。
- `research/codex_power.md`：早期 Codex 电源/音量研究。

## 约束

- 不自动重启；任何 U 盘/ACPI 写入和重启需用户确认。
- 不写内部 ESP；只操作 U 盘 EFI。
- 保留可回退的 S0/S1 配置。

## 2026-10-26 后续执行记录

执行计划见 [EXECUTION-PLAN.md](EXECUTION-PLAN.md)，离线证据和剩余部署门槛见 [OFFLINE-AUDIT.md](OFFLINE-AUDIT.md)。已制作独立内存计数探针并完成 AML 模拟；当前候选默认禁用，macOS 读取工具已实现并交叉编译，尚缺真机验证与现场 EFI 核验。读取说明见 [source/q54reader/README.md](source/q54reader/README.md)。上文单纯 Notify 探针作为历史方案保留。

## 第一轮真机结果

探针已部署并实际启动；自检通过，三次物理短按未观测到 _Q54 进入/返回计数增长。OpenCore 已验证重命名命中。后续转向 EC Query/GPE/SCI。详见 [experiment/RESULT.md](experiment/RESULT.md)。旧文中的 NotificationCount 是 HID 客户端队列统计，不能代表 ACPI Notify 次数。

## 2026-10-28 结论与修复（v0.1.50）

根因：macOS 下 EC 固件不为电源键发 Query 0x54，除非 OS 读过一遍 EC RAM（0x00–0xFF 逐字节）。
Linux 的 ec_sys / 正常 EC 访问会做到这一点；macOS 平时只有电池轮询访问少数几个字节，不够。
排除过的因素：OSYS/_OSI（Linux 加 `acpi_osi=! acpi_osi=Darwin` 仍正常）、HIDD/BTNE、PS/2、EC RAM 状态字节、深度空闲。
开机读一次即可，无需持续轮询（v0.1.49 验证）。

修复：`fix-ecinit/SSDT-PWRBEC.aml` + DSDT 改名 `_REG`→`XREG`（Find `144A065F52454702` / Replace `144A065852454702`，OemTableId "A M I"）。
在 `H_EC._REG(3,1)` 之后、仅 Darwin 下按字节读 EC RAM 一遍，只读。
验证：PWRB AppleACPIButton `EnqueueEventCount` 随按键增长；Ctrl+电源弹关机对话框。
