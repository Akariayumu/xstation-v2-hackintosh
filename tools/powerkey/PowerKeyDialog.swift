// X-STATION V2：短按电源键弹出"关机/取消/关闭屏幕/重启"面板；屏幕关着时短按只点亮屏幕。
// 依赖 v0.1.50 的 EC 修复（PWRB -> AppleACPIButton 已能收到按键）。
// 检测：轮询 PWRB/AppleACPIButton 下 IOHIDEventServiceUserClient 的 DebugState.EnqueueEventCount，
// 计数增加 = 有按键。按住 Control 时跳过（系统自己会弹框）。
// 指纹联动：当会话处于锁屏态（屏幕亮着）或收到 org.xstation.xfinger.willUnlock 后的 2.5 秒内，
// 仅推进按键计数基线而不弹出关机面板，防止按一体式指纹电源键解锁时误弹关机框。
import AppKit
import CoreGraphics
import IOKit
import IOKit.pwr_mgt

let pollInterval: TimeInterval = 0.25
let debounceInterval: TimeInterval = 3.0
let willUnlockNotification = Notification.Name("org.xstation.xfinger.willUnlock")

func log(_ s: String) {
    FileHandle.standardError.write("\(Date()) \(s)\n".data(using: .utf8)!)
}

func run(_ path: String, _ args: [String]) {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: path)
    p.arguments = args
    do { try p.run() } catch { log("\(path) failed: \(error)") }
}

func isSessionLocked() -> Bool {
    guard let dict = CGSessionCopyCurrentDictionary() as? [String: Any] else {
        return false
    }
    if let locked = dict["CGSSessionScreenIsLocked"] as? Bool {
        return locked
    }
    if let lockedNum = dict["CGSSessionScreenIsLocked"] as? NSNumber {
        return lockedNum.boolValue
    }
    return false
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
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { run("/usr/bin/pmset", ["displaysleepnow"]) }
    case NSApplication.ModalResponse(rawValue: NSApplication.ModalResponse.alertThirdButtonReturn.rawValue + 1):
        log("restart"); tellLoginwindow("rest")
    default: log("cancel")
    }
}

final class UnlockCooldownObserver: NSObject {
    var suppressUntil: Date = .distantPast

    @objc func handleWillUnlock(_ note: Notification) {
        suppressUntil = Date().addingTimeInterval(2.5)
        log("received org.xstation.xfinger.willUnlock, suppressing panel for 2.5s")
    }
}

@main
enum PowerKeyDialogApp {
    static var activityToken: NSObjectProtocol?
    static let unlockObserver = UnlockCooldownObserver()

    static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        activityToken = ProcessInfo.processInfo.beginActivity(
            options: .userInitiatedAllowingIdleSystemSleep,
            reason: "power key watcher"
        )

        DistributedNotificationCenter.default().addObserver(
            unlockObserver,
            selector: #selector(UnlockCooldownObserver.handleWillUnlock(_:)),
            name: willUnlockNotification,
            object: nil,
            suspensionBehavior: .deliverImmediately
        )

        var acSleepAssertion: IOPMAssertionID = 0
        let acAssertRet = IOPMAssertionCreateWithName(
            "PreventSystemSleep" as CFString,
            IOPMAssertionLevel(kIOPMAssertionLevelOn),
            "X-STATION V2 AC Display-Sleep Policy (battery S0ix unaffected)" as CFString,
            &acSleepAssertion
        )

        var last = pwrbEventCount()
        var lastPress = Date.distantPast
        var lastAsleep = Date.distantPast
        var displayAsleep = false
        log("started, count=\(last.map(String.init) ?? "nil"), acPreventSystemSleep=\(acAssertRet == kIOReturnSuccess)")

        Timer.scheduledTimer(withTimeInterval: pollInterval, repeats: true) { _ in
            let nowDate = Date()
            let asleep = CGDisplayIsAsleep(CGMainDisplayID()) != 0
            if asleep { lastAsleep = nowDate }
            if asleep != displayAsleep { displayAsleep = asleep; log(asleep ? "display off" : "display on") }
            guard let curCount = pwrbEventCount() else { last = nil; return }
            defer { last = pwrbEventCount() ?? curCount }
            guard let prev = last, curCount > prev else { return }

            let controlHeld = CGEventSource.flagsState(.combinedSessionState).contains(.maskControl)
            let locked = isSessionLocked()
            let decision = decidePowerKeyAction(
                now: nowDate,
                lastPress: lastPress,
                lastAsleep: lastAsleep,
                unlockSuppressUntil: unlockObserver.suppressUntil,
                controlHeld: controlHeld,
                sessionLocked: locked,
                debounce: debounceInterval
            )
            if decision.updateLastPress {
                lastPress = nowDate
            }

            switch decision.action {
            case .ignore:
                if controlHeld {
                    log("press with Control, leave it to the system")
                } else if locked || nowDate < unlockObserver.suppressUntil {
                    log("power key press (count \(prev) -> \(curCount)) suppressed (locked=\(locked), unlockCooldown=\(nowDate < unlockObserver.suppressUntil))")
                }
            case .wakeDisplayOnly:
                log("power key press (count \(prev) -> \(curCount)) while display off, waking display")
                run("/usr/bin/caffeinate", ["-u", "-t", "2"])
            case .showPanel:
                log("power key press (count \(prev) -> \(curCount)), showing panel")
                showPowerPanel()
            }
        }
        app.run()
    }
}
