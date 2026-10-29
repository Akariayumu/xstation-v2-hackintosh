# X-STATION V2 内部扬声器路径研究

日期：2026-10-25  
系统：macOS Sequoia 15.8.1 / 24H32  
状态：Linux 下内部扬声器**能出声**（2026-10-26 Live 基线，见第 11 节），硬件/功放正常；macOS layout 19 有 Engine 但无声。已准备 v0.1.43（只加 `alcverbs=1`）做运行时只读对比  
Codex 只读复核：已完成，`private/audio-research-20261025/codex.md`，SHA-256 `99d53e573d5c8c5dc6855aad6baa18c5a99568a640b853b52698cd33bc0478b0`

## 1. 结论先行

当前 `layout 5` 确实已经生效：

```text
layout-id     = 5
alc-layout-id = 5
AppleHDAEngine layout-id = 5
Output Source = Internal Speakers
```

但它仍然无声，最可能不是“layout 没生效”，而是 **layout 5 的 PathMap 只驱动了 0x14（Bass Speaker），没有驱动 0x1b（主 Speaker）**。

本机 ALC256 codec 的两个输出 pin：

| Node | codec 名称 | 含义 |
|---|---|---|
| `0x14` | `Bass Speaker Playback Switch` | 低音/辅助扬声器 |
| `0x1b` | `Speaker Playback Switch` | 主内部扬声器 |
| `0x21` | `Headphone Playback Switch` | 3.5mm 耳机，已正常 |

因此：

- **layout 5**：输出只到 `0x14` + `0x21`；主扬声器 `0x1b` 没有输出路径，所以听不到正常声音。
- **layout 69**：输出到 `0x1b` + `0x21`；已实测，AppleHDAEngine 未建立、音频输出设备消失，**不能使用**。
- **layout 76**：输出到 `0x1b` + `0x14` + `0x21`；已实测，AppleHDAEngine 未建立、音频输出设备消失，**不能使用**。
- **layout 11/13/21**：仍是 `0x14` 路径，和 layout 5 同类；不应再优先盲测。

## 2. 本机证据

### 2.1 codec dump

来源：

```text
sources/xstation-v2-supplement-20261022-153839/audio/card0-codec#0.txt
```

关键行：

```text
Node 0x14 [Pin Complex]
  Control: name="Bass Speaker Playback Switch"
  Pincap: OUT EAPD Detect
  EAPD 0x2: EAPD
  Pin Default 0x90170110: [Fixed] Speaker at Int N/A

Node 0x1b [Pin Complex]
  Control: name="Speaker Playback Switch"
  Pincap: IN OUT EAPD Detect
  EAPD 0x2: EAPD
  Pin Default 0x411111f0: [N/A] Speaker at Ext Rear
```

`0x1b` 的 pin default 是 N/A，但 codec 明确提供了 `Speaker Playback Switch`、`Amp-Out` 和 EAPD；这说明它是可用的 speaker 输出节点，不能因为 default 为 N/A 就排除。

### 2.2 AppleALC PathMap

来源：

```text
dist/audio/applealc-1.9.8/src/AppleALC-1.9.8/Resources/ALC256/Platforms5.xml
dist/audio/applealc-1.9.8/src/AppleALC-1.9.8/Resources/ALC256/Platforms69.xml
dist/audio/applealc-1.9.8/src/AppleALC-1.9.8/Resources/ALC256/Platforms76.xml
```

实际 PathMap 节点：

```text
layout 5:
  输入: 0x08 -> 0x23 -> 0x1b
  输出: 0x14 -> 0x02, 0x21 -> 0x03

layout 69:
  输入: 0x08 -> 0x23 -> 0x12
  输出: 0x1b -> 0x03, 0x21 -> 0x02

layout 76:
  输入: 0x08 -> 0x23 -> 0x12
  输出: 0x1b -> 0x03, 0x14 -> 0x02, 0x21 -> 0x02
```

这直接解释了为什么 layout 5 的耳机正常、内部扬声器无声：耳机在输出路径里，主扬声器 `0x1b` 不在 layout 5 的输出路径里。

## 3. 下一步单变量测试

已准备配置：

```text
work/efi-b/v0.1.40-wifi-bt-audio-sequoia/configs/config-layout76-applid.plist
work/efi-b/v0.1.40-wifi-bt-audio-sequoia/configs/config-layout69-applid.plist
```

Mac EFI 中已保存：

```text
config-layout76-applid.plist
config-layout69-applid.plist
config-layout76-noaudio-20261025.plist
config-layout5-applid-before76-20261025.plist
```

测试记录与顺序：

1. **layout 76 + alcapplid=76** — 已测，失败
   - 实际 `layout-id=76`、`alc-layout-id=76`；
   - AppleALC 1.9.8 已加载，AppleHDA/AppleHDAController 已加载；
   - 但 `AppleHDAEngine` 数量为 0，`system_profiler SPAudioDataType` 没有输出设备；
   - 已写回 layout 5 备份并重启恢复。
2. **layout 69 + alcapplid=69** — 已测，失败
   - 实际 `layout-id=69`、`alc-layout-id=69`；
   - 与 76 相同：`AppleHDAEngine` 为 0，音频输出设备列表为空；
   - 已回滚 layout 5。
3. **layout 19 + alcapplid=19** — 已测，有设备但无声
   - 实际 `layout-id=19`、`alc-layout-id=19`；
   - `AppleHDAEngine` 存在，`Built-in Output` 存在，Output Source 为 Internal Speakers；
   - PathMap 是 `0x1b -> 0x03` + `0x21 -> 0x02`，说明主扬声器路径已经进入 Engine，但仍无声。
4. 停止换 layout ID。
   - 69/76 无引擎；19 有引擎但无声；11/13/21 仍只走 0x14，不再测试。
5. 下一步：先做 **Linux 播放基线**，再做 **0x1b 运行时读取**。
   - 不需要 AppleALCU；AppleALC 自带 `ALCUserClientProvider`，但必须用 boot-arg `alcverbs=1`（或 HDEF `alc-verbs` 属性）放行；
   - 先在 Linux 下确认内部扬声器是否出声、0x1b/0x14 哪个控制项真正对应扬声器，并采集播放中的 codec 状态；
   - macOS 只加 `alcverbs=1`，用 `alc-verb` 读取 `0x1b` 的 pin control、连接选择、EAPD、amp，再与 Linux 对比；
   - 只有确认第一个异常项后，才做单变量写入；有效后再固化为自定义最小 layout，不用 69/76 整套资源。

## 4. 如果 layout 19 的运行时诊断失败

需要区分：

- **EAPD 未置位**：`0x1b` 有 EAPD 能力；需要 codec verb/自定义 layout 验证。
- **外部 amp/GPIO mute**：需要 DSDT/EC/GPIO 证据，不能盲加 `MuteGPIO`。
- **独立 I2C 功放**：本机 DSDT 有 `HDAC` + `10EC1308`（ALC1308）候选节点，但当前 `_STA` 返回 0；不能直接认定它在使用，也不能直接写 ACPI。
- **AppleALC 平台注入问题**：需要 codec 日志/实际 verbs，而不是继续换 layout ID。

不建议当前阶段：

- 不换 VoodooHDA/SimpleHDA；
- 不盲改 DSDT/GPIO；
- 不同时改 audio + touch + sleep；
- 不用耳机正常证明扬声器路径正常。

## 5. Codex 类似设备调研结论

原始文档：

```text
private/audio-research-20261025/codex.md
SHA-256 99d53e573d5c8c5dc6855aad6baa18c5a99568a640b853b52698cd33bc0478b0
```

关键结论：

- 公开 Linux 修复中，**IPASON SmartBook S1** 与本机同为 ALC256、Subsystem `2782:0206`、控制器 `8086:a0c8`，pin defaults 逐项吻合；最终修复是把被固件标成未连接的 **`0x1b` 声明为内部扬声器**。这不是 X-STATION V2 同型号认证，但路径高度匹配。
- 该 Linux 报告里，保留或禁用 `0x14` 都能出声，决定因素是 `0x1b`；这支持“先让 layout 69 驱动 `0x1b`”。
- AppleALC 1.9.8 的 PinConfigs 中，layout 76 对 `0x1b`、`0x14` 设置 EAPD 并在唤醒时重发；layout 69 也初始化 `0x1b` EAPD，但带有小米专用 coefficient。76 的 4CH 实际结果却是 AppleHDAEngine 不建立，因此实际测试优先级改为 **69 先于 76**。
- `AppleHDAEngine` 显示 7 不等于 AppleALC 选用了资源 7：`alcid` 选择资源，`alcapplid` 控制上报给 AppleHDA 的编号；三值一致只是实验记录方式，不是通用要求。
- 若 69 也失败，应该比较播放中的 DAC stream、连接选择、pin OUT/mute、EAPD、电源和 GPIO，而不是继续枚举 layout ID。
- 当前阶段不引入 SimpleHDA、VoodooHDA 或 DSDT 改动；不要混装音频驱动、复制他机 GPIO/coefficient、扫写寄存器或提高功放增益。

## 6. Claude Code 计划评审（2026-10-26）

原始文档：

```text
private/audio-research-20261025/claude-code-review.md
SHA-256 756fbeddd7808df8fb69b16ace58bd6732c1362fa54c2506b94a2f5283b6bd0d
```

关键修正：

- **AppleALCU 路线是错的**：AppleALC.kext 本身已带 `ALCUserClientProvider`；`ALCUserClientProvider::probe()` 只有在 HDEF 有 `alc-verbs` 属性或 boot-args 有 `alcverbs=1` 时才放行。没有这个开关，`alc-verb` 必然找不到 provider。
- 同时加载 AppleALC 和 AppleALCU 可能注册重复类/双重补丁；**不要再用 AppleALCU**。正确做法是只加 `alcverbs=1`，必要时再加 HDEF `alc-verbs` 属性。
- **必须先取 Linux 播放基线**：确认内部扬声器是否响、0x1b/0x14 混音器哪个真正对应扬声器、播放中的 codec 状态；否则无法区分 codec 侧和功放/GPIO 侧。
- layout 19 有 Engine 但无声，最可能的顺序是：0x1b 连接选择未切到 DAC 0x03 > 0x1b pin-ctl/EAPD 未使能 > 0x14 不在 PathMap 导致功放关联缺失。
- 自定义 layout 必须等运行时 verb 证明有效后再做；先读后写，一次只改一个异常项。

最终顺序：

```text
1. 恢复 U 盘 启动（layout19，无 AppleALCU）
2. Linux 播放基线 + 播放中 codec 状态
3. macOS 只加 alcverbs=1，确认 ALCUserClientProvider/ReadyForALCVerbs
4. 播放中读取 0x1b/0x14/0x21/DAC 的 verb
5. 和 Linux 对比，选第一个异常项做单变量写入
6. 有效后固化为自定义最小 layout
7. 回归耳机/麦克风/冷启动/热重启
8. 仍无声才进入 amp/GPIO/I2C/硬件验证
```


## 7. 2026-10-26 麦克风实测

用户确认内部录音麦克风正常。当前音频结论：

```text
内部麦克风：正常
3.5mm 耳机：正常
内部扬声器：仍无声
```

这说明 AppleHDA 的 capture/input 路径和 ALC256 输入侧基本正常；问题进一步收窄到**输出侧的 0x1b/0x14、EAPD/amp/GPIO 或独立功放**，而不是整个 codec/驱动没有工作。

## 8. 2026-10-26 Codex + Claude Code 扬声器布局联合研究

原始文档：

```text
private/audio-research-20261025/codex-speaker-layout.md
private/audio-research-20261025/claude-code-speaker-layout.md
```

### 8.1 设备识别

- 本机 DMI：`EM_E134_M_V1.0`，BIOS `TGL_E134_V4.1.12`；与 **Eve V 2021（E134）** 高度对应。公开规格写有“两高频 + 两低频扬声器”，但没有 ALC256 接线图，不能直接外推。
- Linux 主线存在针对 **完全相同 SSID `2782:0206`** 的 quirk：只把 **0x1b** 声明为 `0x90170110` 内部扬声器；0x14 保留或禁用都能出声。该修复来自 IPASON SmartBook S1（Emdoor ODM）。
- 这支持 **0x1b 是主扬声器**；0x14 可能是辅助/低频，但本机是否实装未证实。

### 8.2 AppleALC layout 对比

- **layout 19**：`1b -> 03`，`21 -> 02`；能建立 Engine，但无声。
- **layout 20/22/99**：`1b -> 02`，`21 -> 03`；与 Linux codec 快照的默认选择（0x1b 选择 0x02，0x21 选择 0x03）一致。
- **layout 69/76**：能写入编号，但 AppleHDAEngine=0；69 与 19 路由基本相同的差异主要是 coefficient 写入。
- **layout 5**：0x1b 被配置为输入，主扬声器必然不在输出路径。
- **0x14**：layout 19 没有初始化 0x14 EAPD；Linux `alc_auto_setup_eapd()` 会无条件给它 EAPD。这是 layout 19 与 Linux 的明确差异之一。

### 8.3 最可能的下一步单变量

优先级最高的是 **DAC 连接选择**：

1. 先测 **layout 20**（或等价的自定义 `1b -> 02` + `21 -> 03`），这是现有资源中最接近 Linux 默认拓扑的候选；
2. 若 layout 20 能建立 Engine 但仍无声，再加 **0x14 EAPD**，然后检查 0x1b amp mute；
3. coefficient 0x10 bit9 / 0x36 bit13 的清理必须等运行时读数，不能照抄 69；
4. 自定义最小 layout 以 layout 19 的可用引擎为结构基线，先只改 DAC 路由，不同时加 0x14/GPIO/coefficient。

### 8.4 独立功放

- ALC1308/I2C 智能功放基本可以排除：相关 `HDAC` `_STA` 全为 0，NHLT 只有 45 字节，Linux 没枚举 10EC1308。
- 仍不能排除板载模拟功放；需要 Windows 原厂 INF、Linux 播放中 `hda-verb` EAPD 对照或板上丝印，不能用 I2C 扫描/GPIO 试写。

### 8.5 当前不做

- 不盲换更多内置 layout；
- 不照抄 69/76 的 coefficient；
- 不引入 VoodooHDA/SimpleHDA；
- 不盲改 DSDT/GPIO；
- 没拿到 Linux/Windows 播放基线前，不宣称 0x14 一定有扬声器。

## 9. 2026-10-26 layout 20 实测：无 Engine，已回退

layout20 + alcapplid=20 启动后确认：

```text
IODeviceTree layout-id = 20
alc-layout-id = 20
AppleHDAEngine = 0
system_profiler SPAudioDataType = 空
```

说明 `1b -> 02, 21 -> 03` 这套 layout20 资源同样不能建立 AppleHDAEngine；和 69/76 同类问题。已回退：

```text
config-layout19-before-layout20-20261026.plist
SHA-256 7b9bdeea26221c2daeeb1d4ff20a1c116be3f5c05aec5803467f12acc2503392
```

失败配置保留为 `config-layout20-noengine-20261026.plist`。不再盲测内置 layout；若继续，只能走自定义最小 layout 或运行时 verb 路线。

## 10. 2026-10-26 Claude Code 决策评审

Claude Code 结论：

- 自定义 layout 还值得做，但**前置门槛是 Linux 下内部扬声器必须实际出声**；没有这条基线，不再改 EFI，连 `alcverbs=1` 也暂不加。
- layout19 与 layout69 的 layout/Platforms 资源几乎相同；69 多写 4 条 coefficient。69 无 Engine 更可能是 coefficient 写入导致，而不是 0x1b 拓扑错误。
- layout20 与 layout19 同时有输入路径、PathMapID、DAC、pinctrl、DSP 等多处差异，不能把 layout20 无 Engine 归因于 `1b->02` 单变量；所以它不否定自定义 layout。
- 最怀疑 layout19 下 0x1b 仍默认选择 0x02，而音频流走 0x03；必须播放中读 verb 才能确认。
- 优先级：Linux 播放基线 > 蓝牙登录前连接 > 电源键；电源键事件没到 ACPI，`_L43` 已失败，收益低风险高。
- 若 Linux 下扬声器也不响，立即停止音频，转蓝牙。

四步：

1. Linux 基线：amixer 单开 Speaker/Bass Speaker，speaker-test，播放中 codec dump；
2. macOS 只加 `alcverbs=1`，读 0x1b F01/F07/F0C/amp、0x14 F0C、DAC F06；
3. 运行时逐项写，每次只写一条，先 0x1b 连接/DAC，再 mute/EAPD，最后 0x14/coef；
4. 有效后才固化为自定义 layout；无 Engine 时先看 AppleHDA 内核日志，不换编号。

原始评审：`private/audio-research-20261025/claude-code-speaker-decision.md`。

## 11. 2026-10-26 Linux Live 播放基线：扬声器能出声（门槛通过）

记录在 内部交接记录的“2026-10-26 Linux Live 基线”段（Ubuntu 26.10 Live，SSH `<linux-live>`）：

- ALSA：Speaker 100% on，Headphone 0% off，Auto-Mute Disabled；
- `speaker-test -D hw:0,0 -c 2 -t sine`：**用户确认内部扬声器有声音**；
- dmesg：`line_outs=2 (0x1b/0x14)`，上游 `2782:0206` quirk 生效，0x1b 作为内部扬声器。

结论：扬声器、功放、接线都正常，问题在 macOS 侧的 layout/pin/verb。第 10 节的门槛已通过，音频继续，不转蓝牙。

这次没有覆盖到的：

- 播放进行中的 codec dump 没抓。12:53Z 的 pin dump 是在 speaker-test 结束后取的，stream=0；
- 没有分别单开 Speaker / Bass Speaker 试听，0x14 是否实装喇叭仍未知；
- 原始输出没有落盘，只剩 handoff 摘要（命令记录在 `~/.local/share/opencode/log/opencode.log`，2026-10-26T12:51–12:53Z）。

不过连接选择是静态寄存器，旧 supplement dump 已经能给出 Linux 的路由：

```text
0x1b  Connection: 0x02* 0x03   Pin-ctls 0x40 OUT   EAPD 0x2   Amp-Out 0x00 0x00
0x14  Connection: 0x02         Pin-ctls 0x40 OUT   EAPD 0x2   Amp-Out 0x00 0x00
0x21  Connection: 0x02 0x03*   Pin-ctls 0xc0 OUT HP
0x02  Speaker Playback Volume  Amp-Out 0x38 0x38
GPIO  IO[0..2] enable=0
```

也就是 Linux 的扬声器走 **0x1b -> DAC 0x02**，耳机走 0x21 -> 0x03；GPIO 全部未启用，不像是 GPIO 控制的外置功放。layout 19 是 `1b -> 03`、`21 -> 02`，方向正好相反，这仍是第一嫌疑。

### 下一步：v0.1.43 只读对比

- 包：`work/efi-b/v0.1.43-alcverbs-read-sequoia/`，config `1bd4a732`，只在 v0.1.42 上加 `alcverbs=1`；回退 `1d4a43bf`。
- Mac 上运行 `mac-tools/read-alc256-verbs.sh`（AppleALC 1.9.8 自带 `alc-verb`），空闲和播放中各读一遍 0x01 GPIO、DAC 0x02/0x03、0x1b/0x14/0x21。
- 只读不写。拿到读数后再按第 10 节第 3 步，一次只写一条（第一条预计是 `0x1b 0x701 0x01` 或改走 0x02，视读数而定）。


## 12. 2026-10-28 v0.1.43 macOS 运行时读数（layout 19 + alcverbs=1）

- 经 SSH 重启，170 s 回网；`alcverbs=1` 生效，`ALCUserClientProvider` `ReadyForALCVerbs=Yes`，AppleHDAEngine 仍在，输出源 Internal Speakers，无新 panic。
- 原始读数：`work/reports/audio-alcverbs-20261028/alc256-verbs-20261028-100422.txt`（`6fb12194…`）。播放用 SSH 下 `afplay`，未登录图形界面。

播放中（与 Linux 对比）：

| 项 | macOS layout 19 | Linux | 结论 |
|---|---|---|---|
| DAC 0x03 F06 | **0x10**（stream 1，正在出流） | — | 音频流走 0x03 |
| 0x1b F01 连接选择 | **1 → 0x03** | 0 → 0x02 | 与流一致，**路由是通的** |
| 0x1b pin-ctl / EAPD / 电源 / amp | 0x40 OUT / 0x02 / D0 / 0x00 未静音 | 0x40 / 0x02 / D0 / 0x00 | 一致 |
| 0x1b pin config | 0x90170110 | 上游 quirk 同值 | 一致 |
| 0x03 amp-out | 0x57，未静音 | — | 正常 |
| **GPIO（0x01 F16/F17/F15）** | **enable=1，dir=out，data=0** | 3 个 IO 全部 enable=0 | **差异 1** |
| **0x14** | pin config 0x400000f0（layout 19 禁用），pin-ctl 0，amp 0x80 静音 | pin-ctl 0x40，EAPD，amp 0x00 未静音 | **差异 2** |
| coef | 未读 | — | 未知 |

结论：

- 原第一嫌疑“0x1b 停在 0x02、流走 0x03”**被推翻**。codec 内 0x03 → 0x1b 这条路径的 pin、EAPD、电源、静音都和 Linux 一致。
- layout 19 的 ConfigData 里没有 GPIO verb（只有 pin config 和 0x1b/0x21 EAPD）；layout xml 的 `MuteGPIO=0x50010019` 属于 LineIn。GPIO0 被驱动为输出低电平，暂不清楚是谁写的（AppleHDA / BIOS 残留）。
- 下一步候选单变量（运行时写，重启即失效）：
  1. GPIO 回到 Linux 状态：`0x01 0x716 0x00`（关 enable）；不行再试 `0x01 0x715 0x01`（拉高）；
  2. 打开 0x14：`0x14 0x707 0x40` + `0x14 0x300 0xb000`；
  3. 仍无声，再读 coef（需写 coef index，属于写操作，要单独确认）。
- 每一步都需要有人在机器旁听。

## 13. 2026-10-28 运行时单变量写入（全部重启失效），用内置麦克风判定

判定方法：`~/xstation-audio/miclevel`（Swift，AVAudioEngine）在「终端」图形会话里录 4 秒。SSH 进程拿不到麦克风 TCC，需经 `open -a Terminal *.command`；同名 .command 重复打开有时不执行，改用唯一文件名。播放 12 s 1 kHz 正弦 `tone1k.wav`（0.5 幅度，afplay -v 1.0，系统音量 82，未静音），用 Goertzel 算 1 kHz 能量。安静底噪约 -81 ~ -94 dB。

| # | 写入 | 结果（1 kHz dB） |
|---|---|---|
| 0 | 原状态：GPIO0 en=1 dir=out data=1（登录后播放时 AppleHDA 自己拉高） | 用户耳听无声 |
| 1 | `0x01 0x716 0x00`（GPIO 关使能，同 Linux） | -92.7（安静 -93.8），无声 |
| 2 | GPIO0 en=1 dir=out data=0 | -92.4，无声 |
| 3 | GPIO0 恢复 data=1；DAC 0x02 设成与 0x03 相同格式 `0x4031`/stream `0x10`；`0x14 0x707 0x40`、`0x14 0x300 0xb000` | -92.7，无声。发现 0x14、0x02 电源为 D3 |
| 4 | `0x02 0x705 0x00`、`0x14 0x705 0x00`（上电 D0） | -87.1，无声。此时 0x1b 和 0x14 两条路径在 codec 内都完整（D0/OUT/未静音/EAPD/有流） |
| 5 | coef 0x10：`0x0220 -> 0x0020`（清 bit9，Linux `alc_fill_eapd_coef` 做法；coef 0x36 已是 0x5757） | -102.5，无声 |

完整节点状态和 91 个 coef 的读数：`work/reports/audio-alcverbs-20261028/mac-fullstate-20261028.txt`（第 4 步之后、第 5 步之前读取）。

**还没有阳性对照**：这套麦克风判定方法还没在“确定有声”的情况下验证过（例如耳机贴近麦克风）。在验证之前，第 1–5 步的“无声”只能算麦克风读数，要以人耳确认为准。

下一步：
1. 做阳性对照，确认麦克风判定方法可信；
2. 回 Linux Live，用 `hda-verb` 读全部 91 个 coef，以及播放中 0x14/0x1b 的状态，和上面的 Mac 读数逐项 diff；
3. 如果 coef 全部一致，问题就在 codec 之外（PCH GPIO / ACPI `_PS0/_PS3` / EC 控制的功放使能），转查 DSDT 里 HDAS 的电源方法。

## 14. Linux coef 对比与反向验证（2026-10-28 10:55）

- 采集：`scripts/linux_alc256/collect.sh`，结果 `work/reports/audio-alcverbs-20261028/alc256-linux-20261028/`
  （打包 c68eb8d2…）。Linux 麦克风正对照成立：1 kHz 静音 -63.2 dB → 播放 -20.0 dB。
- Linux 播放态：0x14 与 0x1b 都接 DAC 0x02、pin-ctl 0x40、EAPD 2；0x21 选 0x03；codec GPIO 全 0；
  驱动只改 `driver_pin_configs 0x1b=0x90170110`，BIOS 的 0x14 也是 0x90170110。
- coef 对比（0x00–0x5a）只有两项不同：**0x10 Linux 0x0020 / Mac 0x0220**，0x37 Linux 0xfe06 / Mac 0xfe05。
  Linux 0x5b=0x4904，0x5c–0x7f 全 0。idle 与播放 coef 不变；PCH pinctrl 播放前后只差 CNVi 的 pin 198，无功放使能脚切换。
- 反向验证（`linux-reverse-coef-20261028.txt`）：Linux 上把 **coef 0x10 置回 0x0220 扬声器立即哑**（-57 dB），
  清回 0x0020 立即恢复；0x37 改为 0xfe05 不影响。**coef 0x10 bit9 是扬声器开关。**
- 矛盾点：Mac 第 5 步清过 0x10 仍无声。最可能是 AppleHDA/AppleALC 在流启动或 codec 唤醒时把 0x10 写回 0x0220
  （第 5 步之后未回读）。下一步回 Mac：先在播放中清 bit9 并连续回读 0x10，确认是否被改回。

## 15. Mac 实机修复验证（2026-10-28，v0.1.43 干净开机）

- 开机后未做任何写入：coef 0x10=0x0220（bit9 置位）、0x37=0xfe06、0x36=0x5757、0x5b=0x4904；0x1b 选 DAC 0x03（layout 19 原样）。
- 播放 1 kHz 中**只写 coef 0x10 = 0x0020**（`0x20 0x500 0x10` + `0x20 0x400 0x0020`）：
  麦克风 1 kHz **-12.0 dB**（之前所有 Mac 测试约 -100 dB），用户确认**内部扬声器有声**。
- 保持性：闲置 40 s（AFG 进入 D3，F05=0x233）后 0x10 仍为 0x0020，再次播放 -13.2 dB 有声。重启会恢复 0x0220。
- 第 5 步（§13）当时清 0x10 仍无声的原因未查明；当时同时存在 0x14/0x02/GPIO 等运行时改动，干净状态下单改 0x10 即生效。
- 固化选项：A) LaunchDaemon 开机运行 alc-verb 清 bit9（需保留 alcverbs=1）；B) 自编 AppleALC，layout 19 ConfigData 追加
  `02050010 02040020`（必要时同时放 WakeConfigData）。

## 16. 固化：v0.1.44（AppleALC Info.plist layout 19 ConfigData + WakeConfigData 追加 `02050010 02040020`）

重启后无需任何运行时写入，coef 0x10=0x0020，内部扬声器 1 kHz 麦克风 -14.7 dB。**内部扬声器问题已解决。**
根因：ALC256 coef 0x10 bit9 在开机时为 1（扬声器功放关），Linux `alc_fill_eapd_coef` 会清这一位，layout 19 的 ConfigData 不会。
