# 干净 WEX 0.5.0 重写：Sprint 1 进度

计划：`docs/sleep-wake-20261027/WEX-CLEAN-REWRITE-PLAN.md` §8
来源政策：`PROVENANCE-POLICY.md`
输入清单：`input-manifest.json`（由 `scripts/wex_clean_manifest.py` 生成）

本轮全部离线进行：没有接触目标机、U 盘或内部 ESP，没有构建 kext，也没有改动上游源码。

## 任务 1：固定工程起点 —— 完成（Mac 工具链一项待补）

### 1.1 基线

| 项目 | 值 |
|---|---|
| 工作树 | `work/wex-clean`，分支 `xstation/clean-delta` |
| 基线 | 0.5.0 `c6cd8ec5ec5bd64ad3c8a852a30d1f537d819fb1`，tree `15668300…9714` |
| 本地改动 | 无（`git status` 为空） |
| 独立核对 | GitHub 源码包 `downloads/wex-0.5.0/src-c6cd8ec5ec.tar.gz` 与工作树 `diff -rq` 完全一致 |
| 0.6.0 参考 | `work/wex-upstream-060-ref`，`7b6b246d…c0`（只读参考） |

两份上游树原先只放在 `/tmp/opencode`，现已复制进 `work/`，重启后不会丢。

### 1.2 构建依赖（已固定到 `work/wex-clean/`，被上游 .gitignore 忽略）

| 依赖 | 来源 | 身份 |
|---|---|---|
| MacKernelSDK | `work/xe-migration/MacKernelSDK` | 1232 个文件，摘要 `39b591fa…dddb` |
| Lilu.kext 1.7.2 | v22 build deps（历次 Mac 构建所用） | `plugin_start.cpp` `9ebc5413…`，与 v33 构建清单一致 |
| Mac 工具链 | — | **待补**：首次 Mac 构建时记录 |

运行时 EFI 上的 Lilu 也是 1.7.2，但二进制不同（`ff98508b…`，不带 headers）。构建只用到 headers 和 `plugin_start.cpp`，所以这不影响构建。

### 1.3 运行时图形栈身份（新确认）

| 组件 | 结论 |
|---|---|
| framebuffer donor | 目标机上的 `909847cd…`，与 **Graphics 0.2.0 Sequoia.pkg 载荷逐字节相同**，UUID `B6078743…0949`，和 panic 报告一致 |
| Compat | Graphics 0.2.0 Common.pkg 里的 `AppleIntelTGLCompat`，UUID `CC255140-D752-34BE-AE63-593FF13D88C2`，**和 panic 报告的依赖 UUID 完全一致** |

所以 panic 时的系统侧图形栈就是 Graphics 0.2.0，没有其他来源。

### 1.4 Donor 差异（澄清了交接里的未解决问题 2 的一部分）

逐 section 比较 Mach-O：

- `909847cd…`（运行时）对 `a562…`（`sle_Internal/lep`）：`__text`、`__const`、`__cstring`、`__eh_frame` 等**全部代码和常量逐字节相同**。只有 `__DATA,__data` 差 5 字节（kmod 名 `com.apple.driver…` 被改成 `com.xxxxx.driver…`），另外签名长度不同（47104 对 28048）。
- Graphics 0.1.0/0.2.0 的 Sonoma/Sequoia/Tahoe 六份载荷：所有 section 都相同，只有签名（`__LINKEDIT`）不同。
- 上游 fixture `c004…` 不在以上任何一份里，本地仍然没有这个文件。

结论：`a562` 和 `909847` 的差异只是身份改写加签名，可以排除代码差异。`c004` 本身仍然拿不到，所以它和本机 donor 的关系只能间接证明（见 1.5）。

### 1.5 测试基线

**Linux CPU 测试**（`tools/test.py`，clang 18.1.3，用 `-arch` 垫片 `work/wex-clean-tools/bin/clang++`，`ASAN_OPTIONS=detect_leaks=0`）：

- 115 项通过，11 项失败；
- 11 项失败全部是 Darwin 专有：8 个 `*_pmap`/`*_iosurface` 内联汇编依赖 Mach-O 的 `_` 符号前缀（Linux 链接报 `undefined reference to _tglPmapBARFirstPage/_tglSonomaMatrixOne`），`xelp_memory_layout` 和 `tgl_bootstrap_aux` 依赖 macOS 上 `uint64_t` 等于 `unsigned long long`，`small_gt_urb` 在 Linux 下 ASan 报 SEGV（原因未深查，属 GT/URB，与本次范围无关，以 Mac 结果为准）；
- 都不在 TGL 时序/电源范围内。不加垫片时是 106 项通过（另外 9 项只是 `-arch` 参数问题）；
- 日志：`logs/wex-clean-baseline-test-linux-20261027.log`、`logs/wex-clean-baseline-test-linux-shim-20261027.log`。

**Donor replay（等价替换，不是 exact-donor PASS）**：`scripts/wex_donor_equiv_replay.py`

- 先证明运行时 donor 和 `a562` 只有允许的 `__DATA,__data:5` 差异，再只对这串字节把哈希替换成 `c004`；不改上游测试文件；
- `tools/test.py --donor` 列出的 13 项全部通过，其中 `tgl_power_fuses`：`PASS: 11364 production fuse poll cases; exact sites, timeout, read failure, unrelated callers`；
- 反向对照：翻转 `__text` 一个字节，运行器正确拒绝；
- 报告：`logs/wex-clean-donor/equiv-909847/report.json`。

含义：上游 PG234 轮询的匹配签名、调用点（`+0x3A7/+0x549/+0x6DD`）和超时/读失败行为，在本机实际 donor 字节上成立。这是 0.5.0 路线最关键的准入证据。但它不等于 `c004` 和本机 donor 全文件代码相同，因为上游测试只检查它们关心的位置。

### 1.6 待补

1. Mac 上记录工具链版本，并跑一次权威 `tools/test.py`（预期 126 项全部通过），同时跑等价 replay；
2. `c004` 原文件：可以考虑向上游作者确认来源（未经用户同意不对外联系）；
3. 交接文档的小错误：`docs/sequoia-bprime-technical-doc-20261025.md:189` 的 Tahoe 哈希少了一位，实际值是 `181537d02fc25fe9…256f`。

## 任务 2：donor 契约 —— 完成（离线部分）

详见 `DONOR-CONTRACT.md`，机器契约 `donor-contract.json`（37 个点位，含重定位掩码；2026-10-27 加入 5 个 B5 唤醒/DP 恢复点位），校验器 `scripts/wex_donor_contract.py`。

- 6 份 donor（运行时、`a562`、Graphics 0.1.0/0.2.0 各变体）全部 PASS；反向对照（改 PG2 的 `bt` 位）FAIL。
- timing 根源：`setupDisplayTiming` 是 void。AGDC 回调失败或索引越界时 `out` 不写，`setDisplayMode` 照样交给 `hwSetMode`。消费者没有错误出口，`SetupTimings`/watermark 遇到零值会 #DE。
- 建议的 T1：hook `setupDisplayTiming`，用哨兵检测 native 失败，失败时用本次调用的 `in` 填充（与 native 透传分支等价）。合法输入下完全透明。
- 唯一 ABI 已证明的拒绝出口是 `validateModeDepth`=0，但它有 rip-relative prologue，又与其他函数共用，暂时阻断。外层 `hwSetMode` 不能作为拒绝点。
- 新阻断项：唤醒和 `recoverDPLink` 路径用 `fb+0x3FD8` 索引重建 timing，没有 `-1` 检查（Stage 3 前必须解决）。

## 任务 3：行为规格测试 —— 完成（测试处于预期的红灯状态）

新文件都在 `work/wex-clean`（未提交）：

| 文件 | 内容 |
|---|---|
| `tests/tgl_timing_guard.cpp` | 纯 helper 规格：只动 3 个探针字段、native 成功时透明、失败时用**当前** `in` 修复（不复用旧结果）、部分覆盖记为 `Inconsistent`、NULL/重叠时不启用 |
| `tests/tgl_timing_guard_native_fixture.cpp` + `tests/tgl_timing_guard_native.py` | 把 donor 映射进内存，**直接执行真实的** `setupDisplayTiming`（`0x6814C`）。8 个外部调用全部换成 mock，AGDC 回调也是 mock。donor 固定为 `909847cd…` |
| `tools/test.py` | 新增 `--tgl-donor` 选项，用来接入上面的 native replay（上游 `--donor` 固定为 c004，两者不能混用） |

native replay 共 720 种组合：AGDC 结果 × 索引（0/1/2/-1/7）× 三条成功路径 × `0x1E9`/`0x1EA`/旧 `0x3FD8` × 两种 timing × 两种初始垃圾。

- `--native-only` 现在就能通过：在 576 个失败组合里，native 都没有改动输出和 framebuffer，只打了一行日志，而 `setDisplayMode` 仍会去使用这块输出。这是由执行结果证明的反例，不是从静态分析推出来的。144 个成功组合都写满了探针字段。
- 带 guard 的版本要求：成功时输出、整个 framebuffer 和所有 mock 计数都与 native 逐字节相同；失败时输出等于 `in`，其余与 native 相同。

接口约定（任务 4 实现，必须兼容 C++14，因为 native fixture 用 `-std=c++14` 编译）：

```cpp
struct TGLTimingGuard {
  static constexpr size_t size = 0xA0;
  enum Outcome : uint8_t { Native, Repaired, Inconsistent };
  static bool applicable(const uint8_t *in, const uint8_t *out); // 非 NULL 且不重叠
  static void arm(uint8_t *out);                                 // 只写 0x28/8、0x40/4、0x50/4
  static Outcome settle(uint8_t *out, const uint8_t *in);
};
```

验证测试本身是否有效：在 scratchpad 里写了一份参考实现和 4 个故意写错的版本（不放进工作树）。参考实现两项测试都通过；每个错误版本都至少被一项测试抓到：

| 错误版本 | unit | native |
|---|---|---|
| 什么都不做 | FAIL | FAIL |
| 总是复制 `in` | FAIL | FAIL |
| 只探测 2 个字段 | FAIL | pass |
| 不检查重叠 | FAIL | pass |

全量测试（Linux，日志 `logs/wex-clean-task3-test-linux-20261027.log`）：115 项通过，原来的 11 项平台差异照旧，另外 2 项新测试因为 `kern_tgl_timing_guard.hpp` 还不存在而失败。这是预期的红灯。

留给任务 4 的测试：wrapper 安装准入（opt-in 参数、契约字节、route 失败时不发布 Ready）、对照 donor 检查生产常量的 route 测试、诊断计数。这些依赖生产代码，到时一起写。

## 任务 4：最小 timing delta —— 完成（离线；未构建 kext）

改动（`work/wex-clean`，未提交；完整列表见 `input-manifest.json` 的 `changes`）：

| 文件 | 内容 |
|---|---|
| `WhateverXe/kern_tgl_timing_guard.hpp` | 新增纯 helper：`applicable/arm/settle`，兼容 C++14 |
| `WhateverXe/kern_xe.cpp` | +53 行：include；`processKextFull` 里新增一个安装块；新增 `wrapTGLDisplayTiming` |
| `WhateverXe/kern_xe.hpp` | +4 行：`orgTGLDisplayTiming`、`tglDisplayTimingReady`、4 个计数、wrapper 声明 |
| `tests/tgl_display_timing_route.py` | 从源码里抽出生产安装块和 wrapper，对着 mock 执行 |
| `tests/fixtures/boot_argument_names.json` | 登记 `-wex-tgl-sdt-guard`（上游的参数名空间回归测试要求） |
| `tools/test.py` | 接入新测试；`--tgl-donor` 跑两项 donor 版本 |

**安装条件（全部满足才 route，否则发布 `TGLDisplayTimingGuardReady=false`）：**

- boot-arg `-wex-tgl-sdt-guard`（新名字，不沿用 B′ 参数），并且 `tglActive`；
- `XELP::kernelOS==Sequoia`，物理 ID `0x9A498086`、revision 1、没有被 override。revision 来自 `logs/v0.1.16-after-rollback-state.txt` 里 system_profiler 的 `Revision ID: 0x0001`；
- 符号解析到 `0x6814C`，并且 7 个点位共 135 字节完全匹配（含 `setDisplayMode` 的两处调用点；memmove 的重定位字节不参与比较）；
- 只 route 这一个函数，不改任何上游 hook。

**运行时行为：**

- 进入条件：fb 的 controller 必须是 `tglObservationController`，`in/out` 不能为 NULL，也不能重叠。不满足就原样调用 native，计入 `Bypassed`；
- `arm` → native → `settle`。结果是 `Native` 时什么都不发布；`Repaired` 或 `Inconsistent` 时写 `TGLDisplayTimingGuardLast` = `{outcome, fb 索引, pixelClock, hActive, vActive, Repaired 计数, Inconsistent 计数}`；
- 不加锁、不分配、不睡眠，计数用原子加。只有修复或异常时才调用 `setProperty`，与上游 `wrapTGLModeTiming` 在同一个上下文里做的事一样。

**测试：**

- `tgl_display_timing_route` 覆盖安装门槛：135 处单字节翻转、3 种错误 OS、revision/identity/override、符号地址错 1 字节、route 失败、未请求时什么都不发布。也覆盖 wrapper 的各种分支：bypass 四种情况、透明、修复和记录内容；
- 全量 Linux：**119 项通过**，原有的 11 项平台差异照旧，新测试全部通过（`logs/wex-clean-task4-test-linux-20261027.log`）；
- 上游 donor 等价 replay：**13/13 仍然通过**（`logs/wex-clean-donor/equiv-909847-task4/`），PG234 没有受影响。

**需要用户知道的取舍：**

1. **范围按 controller，不按 eDP 端口。** 计划 §3.3 写的是只在内置 eDP 路径启用。但 native 失败时，外接屏的 fb 同样会拿到未初始化的 timing，而且失败那一刻 path/port 不一定已经绑定。修复内容与 native 自带的透传分支完全一致，所以没有加端口限制。如果要严格按计划只覆盖 eDP，改动很小。
2. **没有写进公开 README。** 上游只在 README 里列必需参数，诊断类参数（`-wex-tgl-off`、`-wex-noaccel` 等）都不在里面，上游测试也特意让参数登记独立于文档。这是本机实验参数，所以按同样方式处理。
3. 唤醒和 `recoverDPLink` 的阻断项 B5 不在本次范围内，睡眠继续禁止。

## 任务 5：PG234 诊断与离线资格审查 —— 完成（含 Mac 构建）

### 5.1 PG234 诊断（只记录）

- 新增 `WhateverXe/kern_tgl_power_guard.hpp`（`TGLPowerFuseLog`）。
- `wrapTGLRegisterRead` 里只改了一处：`waitPowerWellFuse` 的结果先存进局部变量 `poll`，记录后再返回 `poll.value`。**返回值、10 µs/1000 µs 预算、匹配点位、PG1 处理都没变**，也没有第二次调用 helper。
- 记录用 16 个 uint64 原子计数：每个 PG 的 Ready/Timeout/InvalidMMIO 次数和最长等待，以及最后一次事件。不加锁、不分配、不碰 IORegistry。
- 只在 `TGLPG234PollReady` 为真时，用上游已有的 `OSSerializer` 方式发布 `TGLPowerFuseLive`。查询 ioreg 时才序列化。
- 不需要额外参数：它只是被动记录。
- `0xFFFFFFFF` 单独记为 `InvalidMMIO`，按计划 §3.4，出现就判这轮硬件测试失败。
- 上游 `tests/tgl_power_fuses.py` 只**新增**了日志断言，原有断言一条没改，仍然是 11364 例通过。新增 `tests/tgl_power_guard.cpp`。

### 5.2 离线验证

| 项目 | 结果 |
|---|---|
| Linux 全量测试 | **120 通过**，原有 11 项平台差异照旧（`logs/wex-clean-task5-test-linux-20261027.log`） |
| 上游等价 donor replay | **13/13**（`logs/wex-clean-donor/equiv-909847-task5/`），其中 `tgl_power_fuses` 在本机 donor 字节上 11364 例通过 |
| 内核编译参数下的语法检查（Linux clang 18，`-fapple-kext -nostdinc` + MacKernelSDK + Lilu headers） | `kern_xe.cpp` 0 错误；警告与 0.5.0 基线**完全相同**（11 条，都是上游原有的） |
| 读数工具 | `scripts/wex_decode_guard_props.py`：在目标机上执行 `ioreg -l -w0 -d 1 \| python3 …`，解码四个属性并给出判定 |

Linux 的结果都只是旁证；权威结果以 Mac 构建和 Mac 测试为准。

### 5.3 GO / NO-GO

| 门槛 | 状态 |
|---|---|
| 来源：干净 0.5.0 + 本地增量，没有使用禁用来源；改动完整列在 `input-manifest.json` 的 `changes` | GO |
| PG234 返回值和预算不变 | GO（11364 例 + donor replay） |
| T1：合法输入透明、失败时用当前输入修复、安装门槛 | GO（720 例 native replay + 135 处字节翻转） |
| 诊断能区分安装就绪、实际命中、修复、超时、无效读取 | GO（`*Ready` / `TGLPowerFuseLive` / `TGLDisplayTimingGuardLast`） |
| Mac RELEASE 构建 + dSYM + `build-manifest.json` + 工具链记录 | **GO**（§5.4） |
| Mac 上的权威 `tools/test.py --tgl-donor` | **GO**：131/131 |
| U 盘 P0 回退（移除 Q54 诊断） | **待定**（B1） |
| 用户确认加载新 kext | **待定**（B3） |
| 系统睡眠 / 唤醒路径 | **NO-GO**（B5，不在本候选范围内；睡眠继续禁止） |

**结论：候选已构建并通过 Mac 上的全部测试。上机前提仍是 NO-GO，要等 B1（U 盘回退）和 B3（用户确认加载）解除。** Stage 1 canary 只验证显示关闭和打开（DC9 进出、PG2 重新使能），不做系统睡眠。

## 文件索引

```text
work/wex-clean/                         干净 0.5.0 + 固定依赖
work/wex-upstream-060-ref/              0.6.0 参考
work/wex-clean-tools/bin/clang++        Linux 测试垫片（只去掉 -arch x86_64）
scripts/wex_clean_manifest.py           输入清单生成器
scripts/wex_donor_equiv_replay.py       等价 donor replay
scripts/wex_donor_contract.py           donor 契约生成/校验
docs/wex-clean-rewrite/DONOR-CONTRACT.md / donor-contract.json
private/wex-clean-inputs/gfxpkg-payloads/  Graphics 0.1.0/0.2.0 解出的 framebuffer/Compat
docs/wex-clean-rewrite/input-manifest.json
logs/wex-clean-*                        测试日志
```

### 5.4 Mac 构建（2026-10-27，在目标机上进行；没有加载 kext，没有碰 EFI）

记录：`mac-build-20261027.json`；产物和日志：`private/wex-clean-builds/20261027-c25d19fe/`。

| 项目 | 值 |
|---|---|
| 主机 | 目标机，macOS 15.8.1 24H32 |
| 工具链 | CommandLineTools 16.4，Apple clang 17.0.0（clang-1700.0.13.5），ld-1167.5，SDK 15.5 |
| 源码 | 目标机副本与本地树逐文件一致（摘要 `f42592f4…`）；`build-manifest.json` 里的源码和头文件哈希都与本地一致 |
| **WhateverXe.kext**（RELEASE） | sha256 `c25d19fe…8913`，UUID `1E0F8F7B-7273-3D00-9054-DA9E8EDAE499`，版本 0.5.0，dSYM 的 UUID 一致，未签名（与以往 OC 注入方式相同） |
| 编译警告 | 在同一台机器上构建了一份未修改的 0.5.0 作对照（`453767f3…`）：两边警告完全相同（各 182 条），0 错误 |
| `tools/test.py --tgl-donor`（系统 framebuffer `909847cd…`） | **131 通过，0 失败**。Mac 上没有 Linux 那 11 项平台差异；native replay 在 macOS 上直接执行了 donor 代码 |
| 等价 donor replay | 13/13 |
| donor 契约（系统 framebuffer） | PASS |

部署注意：

- 只替换 `WhateverXe.kext`；运行时的 AppleIntelTGLCompat 仍用 Graphics 0.2.0 自带的那份（UUID `CC255140`）。这次构建出的 Compat 不部署。
- 当前目标机加载的是 WEX **0.4.0**（UUID `620A29D9…`）。换成本候选，实际上同时引入了 0.4.0→0.5.0 的全部上游改动（包括 PG234）和本地增量。所以 Stage 1 第一次上机建议**不加** `-wex-tgl-sdt-guard`：先只看 0.5.0+诊断，单一变量是 PG234；下一轮再加参数。
- 上机后用 `kextstat` 核对 UUID 是否为 `1E0F8F7B…`，用 `scripts/wex_decode_guard_props.py` 读诊断。
- [B5 唤醒路径分析](B5-WAKE-PATH-ANALYSIS.md) — 2026-10-27 草案：路径 A/B、风险 A1/A2、TGLWakeIndexLast 记录器与 T2 门设计
