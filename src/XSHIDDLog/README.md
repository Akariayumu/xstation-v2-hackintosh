# XSHIDDLog — X-STATION V2 / S1

> 2026-10-29 补充：这是电源键调查期间写的只记录日志的驱动。下文是 10-26 写作当时的状态；之后它从 v0.1.42 起一直随 EFI 加载，
> 没有发现问题。它对功能没有作用，可以和 `SSDT-HIDDSTA-v2`、HIDD 改名一起去掉。

这是一个仅记录事件的 `IOService` 驱动，personality 匹配
`IOProviderClass=IOACPIPlatformDevice`、`IONameMatch=INTC1051`。
没有 HID 设备、按键映射、电源管理注册、主动系统睡眠请求、EC 访问或 AML 方法求值；
包括 `_DSM`、`HDEM`、`HIDX` 在内的任何 ACPI 方法都不会由本驱动调用。

**当前交付状态：源码及 kmod 元数据在 Linux / clang 18 下编译通过；本地
cctools ld64 253.9 已生成 `MH_KEXT_BUNDLE`。** 首版 bundle 曾在目标
macOS 15.8.1 上用 `kmutil print-diagnostics -p ... -z` 检查通过（依赖解析
正常、ownership 修正后为 loadable）。之后为通知参数和 boot-arg 解析做了
小幅兼容补丁，补丁后的 bundle 仍需在目标机重复检查。
没有在目标 macOS 上验证实际装载、匹配、通知投递或卸载。

## 接收通知的依据与边界

本地 SDK：

```text
./MacKernelSDK/Headers
```

- `IOKit/IOService.h` 声明
  `virtual IOReturn message(UInt32 type, IOService *provider, void *argument = NULL)`，
  文档说明它用于接收已附着 provider 发来的消息。
- `IOKit/acpi/IOACPITypes.h` 定义 `kIOACPIMessageDeviceNotification`，
  注释说明 ACPI Notify 发送给所有 clients 和 interested parties，参数携带 `UInt32` 通知码。
  实现仅在此类型且 provider 等于当前 provider 时，从非空 `argument` 复制 `UInt32`，
  不保留该指针；其他消息交给 `IOService::message()`。
- `messageClients()` 最终调用 `applyToInterested(gIOGeneralInterest, ...)`；XNU 的
  `applyToInterested()` 会先 `applyToClients()`，再通知 interest parties。因此
  普通子节点和 general-interest 注册都可能收到通知。当前版本两者都保留：
  `message()` 作为普通 client 路径，`registerInterest(gIOGeneralInterest, ...)`
  作为显式 interest 路径。
  不需要 `provider->open()` 或调用 ACPI 方法来建立这条 API 接收路径。
  之前的“messageClients 只发给 interested parties”判断是错误的；0 计数不能
  据此归因于 message() 路径。
- Apple 公开的 [IOACPITypes.h](https://github.com/apple-oss-distributions/IOACPIFamily/blob/main/IOACPITypes.h)
  和 [IOService.h](https://github.com/apple-oss-distributions/xnu/blob/main/iokit/IOKit/IOService.h)
  可用于对照；实际编译以给定本地 SDK 为准。

这些定义支持此实现方案，但不能证明 macOS 15.8.1 上 HIDD 一定会向本客户端投递通知，
也不能证明固件无需额外启用就会产生 `0xC0`。S1 刻意不通过 AML 启用设备。
如果匹配后计数始终为零，只能判定“未观测到有效通知”，不能推断硬件没有事件。
本版本不是已在真机验证的驱动。

## IORegistry 与文本日志

属性在 `XSHIDDLog` 子节点上，不写入 HIDD provider 的属性表：

| 属性 | 类型 | 含义 |
| --- | --- | --- |
| `S1NotifyCount` | 64 位 OSNumber | 本次 start 后有效 ACPI Notify 总次数，初始 0 |
| `S1InterestCount` | 64 位 OSNumber | 经 `gIOGeneralInterest` 收到的次数，初始 0 |
| `S1MessageCount` | 64 位 OSNumber | 经直接 `message()` 收到的次数，初始 0 |
| `S1LastNotify` | 32 位 OSNumber | 最近通知码，初始 0；`0xC0` 在十进制输出中为 192 |
| `S1LastNotifyTime` | 64 位 OSNumber | 最近接收时刻的 uptime 纳秒值，初始 0；不是 Unix 时间 |
| `S1LastArgSize` | 64 位 OSNumber | 最近一次消息的 `argSize`，初始 0 |
| `S1ProviderName` | OSString | `provider->getName()`，预期为 `HIDD` |

计数包含所有有效通知码，未只过滤 `0xC0`。空参数被拒绝，不计数。
`clock_get_uptime()` 经 `absolutetime_to_nanoseconds()` 换算时间，
时间用于同次启动内比较，不保证覆盖机器睡眠的时间。

每次通知更新属性；文本 `IOLog` 只打印前 16 次及此后每 256 次，避免大量刷屏。
使用 SDK 的 `IOLock` 串行化 start、stop 与回调内状态更新，锁保留至 `free()`。
互斥锁争用时线程可能阻塞，这不等于请求机器睡眠；代码没有主动延时或系统睡眠操作。
属性写入可分配内存，应在 ACPI 正常线程回调上下文运行，不能将此回调当成硬中断处理函数使用。
属性更新失败时返回错误，内部计数仍已递增；IORegistry 可能暂时保留旧值。
多个属性不是一个原子快照，读者可能读到跨两次通知的数据。

`-xshiddoff` 存在时，`probe()` 拒绝匹配，`start()` 也做防御检查。
此参数不阻止系统装载 bundle 本身，也不是运行时开关。

## 构建

需要 Bash、Python 3、clang 18、LLVM Mach-O 查看工具和 SDK 内的
`Library/x86_64/libkmod.a`。不依赖 Xcode，不调用安装工具。

```bash
cd /tmp/opencode/xshidd
./build.sh --objects-only
./build.sh
```

第一条构建命令退出 0，产生 `build/XSHIDDLog.o`、`build/kmod_info.o` 和
`build/object-imports.txt`；它只证明可编译，不产生可加载 kext。
第二条使用本地 cctools ld64 253.9 完整链接，已生成
`build/XSHIDDLog.kext`，文件类型为 `MH_KEXT_BUNDLE`。
`ld64.lld-18` 仍不支持 `-kext`，因此不能作为本工程的默认链接器。
cctools ld64 使用旧的 `-macosx_version_min`/`-sdk_version` 参数；脚本会自动
按链接器名称选择参数，也允许手工指定：

```bash
LD64=/absolute/path/to/cctools/ld64 ./build.sh
```

cctools ld64 的链接通过和脚本的文件类型/导入检查，仍不等于目标内核的
KPI 符号解析、签名策略或真机装载验证。之后必须继续在目标 macOS 上做
`kmutil print-diagnostics`、依赖、装载和运行验证。
`SDK_HEADERS`、`KMOD_LIB`、`CLANG`、`KEXT_NM`、`KEXT_OBJDUMP` 也可覆盖。
`KMOD_LIB` 默认从 `SDK_HEADERS` 所在 SDK 推导。输出路径固定在当前源码目录的 `build/`。

只有链接检查通过后才写入 `build/XSHIDDLog.kext/Contents/MacOS/XSHIDDLog`。
完整构建成功时会同时生成 `Contents/Info.plist`、`build/macho-header.txt` 和
`build/undefined-symbols.txt`。每次构建会先移除旧 bundle 可执行文件，避免误用过期产物。

代码中的外部内核 API 均在本地 SDK 头文件中声明。`_start`、`_stop`、
`_realmain`、`_antimain` 是标准 kmod 链接胶水，由生成的 `kmod_info.c` 与 SDK 的
`libkmod.a` 共同提供；不是额外调用的私有系统入口。
不手写或导入 `OSRuntimeInitializeCPP` 等未由这里的 SDK API 提供的运行时入口。
x86_64 的 C++ 初始化由内核 kext 装载流程处理。除 `-fno-use-cxa-atexit` 外，
clang 18 还需要 `-mllvm -disable-atexit-based-global-dtor-lowering`，
保留 `__mod_term_func` 并避免后端重新引入用户态 `__cxa_atexit`。
仅编译模式也检查这些用户态导入，不允许以 stub 掩盖依赖。

`OSBundleLibraries` 声明 iokit、libkern、mach、unsupported KPI；
其中 boot-arg 的 `PE_parse_boot_argn` 属于依赖兼容性需要关注的接口。
声明这些库不等于已经验证目标内核导出。继承自 `IOService` 的虚表会引入大量基类符号，
包括电源管理方法地址；这些导入不表示本驱动主动调用了这些方法。
脚本使用动态未解析符号链接，不能替代目标系统上的 KPI 符号解析验证。

## 后续人工注入与观察（本次未执行）

当前已能产生候选 bundle，但尚未注入；以下步骤必须经用户确认并完成目标机
`kmutil` 检查后人工执行。本工程不提供自动复制、安装、修改配置、重启或
卸载脚本。

1. 先保留可工作的启动环境和原配置副本；仅在受控测试启动项中加入
   `XSHIDDLog.kext`。如果使用 OpenCore，其 `Kernel/Add` 条目的路径字段应为：
   `BundlePath=XSHIDDLog.kext`、`ExecutablePath=Contents/MacOS/XSHIDDLog`、
   `PlistPath=Contents/Info.plist`、`Arch=x86_64`。
   实际启用、版本过滤及现有装载策略由维护者审查；本文不要求改变安全策略。
2. 使用不含 `-xshiddoff` 的测试启动项观察；正常应在 HIDD 下出现 `XSHIDDLog` 节点。
3. 只读观察示例，均需在目标 macOS 上人工执行：

   ```bash
   ioreg -p IOService -r -c XSHIDDLog -l -w 0
   log show --last 10m --style compact --predicate 'eventMessage CONTAINS "XSHIDDLog"'
   hidutil list
   ```

4. 先记录基线，再通过常规按钮动作观察是否增长、最近码是否为 `0xC0`。
   不需要也不应为 S1 验证主动触发睡眠。
   此驱动不会让 `hidutil list` 增加 Intel HID 5-button array。
   若没有日志，以 IORegistry 属性为主要证据；日志可见性受系统日志设置影响。
5. 另一次受控启动带 `-xshiddoff` 时应没有成功附着的 `XSHIDDLog` 节点。
   本次没有执行这些启动测试。

## 回滚与已知风险

人工回滚首选在测试启动项中添加 `-xshiddoff`，使驱动拒绝匹配；
如需彻底阻止装载，禁用对应 `Kernel/Add` 条目并恢复原启动配置，之后再清理 bundle。
应使用已有可工作的启动环境完成恢复，不依赖故障系统实时卸载。
这些都是后续人工维护说明，本次未修改 EFI、U 盘、系统、ACPI、OpenCore 或硬件。

即使只是日志驱动，内核代码仍存在 panic、ABI 不匹配、内存分配失败、
通知风暴增加回调开销等风险。默认匹配类别也可能与未来同设备驱动竞争，
测试时应确认实际附着关系。尚未验证目标系统导出、签名/装载策略、回调上下文、
运行时停用及完整装载生命周期。
S1 不改变固件事件使能状态，也不替换其他设备已有的按键或电源处理逻辑。

## S2 边界

S2 的候选范围是：在 S1 真机验证基础上，仅对 `0xC0` 做只读事件解码，
审查 `_DSM` UUID `eeec56b3-4442-408f-a792-4edd4d758054` 的实际参数/修订号，
再考虑功能 4（HDEM）的 HIDX 读取、返回类型校验及独立错误计数。
AML 方法即使名义上是读取，也可能有固件副作用，必须先审查实际 DSDT。
本次没有实现上述调用。

S2 仍应保持不发布 HID、不合成按键、不请求系统睡眠、不写 EC，
不擅自添加固件启用/禁用、ACPI 补丁或电源管理行为。
HID 按键映射和任何睡眠动作需要独立设计与明确授权。
