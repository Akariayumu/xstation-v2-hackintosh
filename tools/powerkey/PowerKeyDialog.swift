// X-STATION V2：短按电源键弹出"关机/取消/关闭屏幕/重启"面板；屏幕关着时短按只点亮屏幕。
// 依赖 v0.1.50 的 EC 修复（PWRB -> AppleACPIButton 已能收到按键）。
// 检测：轮询 PWRB/AppleACPIButton 下 IOHIDEventServiceUserClient 的 DebugState.EnqueueEventCount，
// 计数增加 = 有按键。按住 Control 时跳过（系统自己会弹框）。
// 面板：系统没有公开接口能调出 Control+电源的那个面板（AppleScript 只有带 60 秒倒计时的关机确认框，
// 模拟的 Control+电源键也不被系统接受），所以自己画一个同样四个按钮的面板，
// 关机/重启发给 loginwindow（kAEShutDown/kAERestart），与系统面板一致：先让各应用正常退出。
// 本机系统睡眠未验证（SleepDisabled=1，见 docs/sleep-wake-20261027），所以用"关闭屏幕"（pmset displaysleepnow）代替"睡眠"。
// 不需要辅助功能/输入监控权限。
import AppKit
import IOKit

let pollInterval: TimeInterval = 0.25
let debounce: TimeInterval = 3.0

func log(_ s: String) {
    FileHandle.standardError.write("\(Date()) \(s)\n".data(using: .utf8)!)
}

func run(_ path: String, _ args: [String]) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: path)
    p.arguments = args
    do { try p.run() } catch { log("\(path) failed: \(error)") }
}

// 返回 PWRB 下所有 user client 的 EnqueueEventCount 之和；找不到返回 nil
func pwrbEventCount() -> Int? {
    var iter: io_iterator_t = 0
    guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleACPIButton"), &iter) == KERN_SUCCESS else { return nil }
    defer { IOObjectRelease(iter) }
    var total: Int? = nil
    while case let button = IOIteratorNext(iter), button != 0 {
        defer { IOObjectRelease(button) }
        var parent: io_registry_entry_t = 0
        guard IORegistryEntryGetParentEntry(button, kIOServicePlane, &parent) == KERN_SUCCESS else { continue }
        var name = [CChar](repeating: 0, count: 128)
        IORegistryEntryGetName(parent, &name)
        IOObjectRelease(parent)
        guard String(cString: name) == "PWRB" else { continue }

        var children: io_iterator_t = 0
        guard IORegistryEntryGetChildIterator(button, kIOServicePlane, &children) == KERN_SUCCESS else { continue }
        while case let child = IOIteratorNext(children), child != 0 {
            defer { IOObjectRelease(child) }
            guard let state = IORegistryEntryCreateCFProperty(child, "DebugState" as CFString, kCFAllocatorDefault, 0)?
                    .takeRetainedValue() as? [String: Any],
                  let n = state["EnqueueEventCount"] as? Int else { continue }
            total = (total ?? 0) + n
        }
        IOObjectRelease(children)
    }
    return total
}

// 发给 loginwindow 的 Apple 事件：shut / rest
func tellLoginwindow(_ code: String) {
    run("/usr/bin/osascript", ["-e", "tell application \"loginwindow\" to «event aevt\(code)»"])
}

func showPowerPanel() {
    NSApp.activate(ignoringOtherApps: true)
    let alert = NSAlert()
    alert.messageText = "你确定要关闭电脑吗？"
    alert.icon = NSImage(named: NSImage.computerName)
    alert.addButton(withTitle: "关机")                                    // 回车
    alert.addButton(withTitle: "取消").keyEquivalent = "\u{1b}"           // Esc
    alert.addButton(withTitle: "关闭屏幕")
    alert.addButton(withTitle: "重启")
    alert.window.level = .modalPanel
    switch alert.runModal() {
    case .alertFirstButtonReturn: log("shut down"); tellLoginwindow("shut")
    case .alertThirdButtonReturn:
        log("panel: turn display off")
        // 稍等让点击/按键松开，避免刚关就被这次输入唤醒
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { run("/usr/bin/pmset", ["displaysleepnow"]) }
    case NSApplication.ModalResponse(rawValue: NSApplication.ModalResponse.alertThirdButtonReturn.rawValue + 1):
        log("restart"); tellLoginwindow("rest")
    default: log("cancel")
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
// 关屏后不被 App Nap 节流（不阻止系统/显示器空闲休眠）
let activity = ProcessInfo.processInfo.beginActivity(options: .userInitiatedAllowingIdleSystemSleep, reason: "power key watcher")

var last = pwrbEventCount()
var lastPress = Date.distantPast
var lastAsleep = Date.distantPast   // 最近一次看到屏幕处于关闭状态的时间
var displayAsleep = false
log("started, count=\(last.map(String.init) ?? "nil")")

Timer.scheduledTimer(withTimeInterval: pollInterval, repeats: true) { _ in
    let asleep = CGDisplayIsAsleep(CGMainDisplayID()) != 0
    if asleep { lastAsleep = Date() }
    if asleep != displayAsleep { displayAsleep = asleep; log(asleep ? "display off" : "display on") }
    guard let now = pwrbEventCount() else { last = nil; return }
    defer { last = pwrbEventCount() ?? now }   // 面板关闭后重取基线，忽略面板打开期间的按键
    guard let prev = last, now > prev else { return }   // 首次出现或计数重置（WindowServer 重启）时只更新基线

    if CGEventSource.flagsState(.combinedSessionState).contains(.maskControl) {
        log("press with Control, leave it to the system")
        lastPress = Date()
        return
    }
    if Date().timeIntervalSince(lastPress) < debounce { return }
    lastPress = Date()
    // 屏幕关着（或这次按键刚把它点亮）：只唤醒屏幕，不弹面板
    if Date().timeIntervalSince(lastAsleep) < 1.5 {
        log("power key press (count \(prev) -> \(now)) while display off, waking display")
        run("/usr/bin/caffeinate", ["-u", "-t", "2"])
        return
    }
    log("power key press (count \(prev) -> \(now)), showing panel")
    showPowerPanel()
}
app.run()
