// 只读抓取 Wacom 056A:48EE 的原始 HID input report（不 seize，不影响系统驱动）。
// 用法：hidsniff [秒数]   输出：时间 rid=xx len=n 十六进制
import Foundation
import IOKit.hid

let seconds = Double(CommandLine.arguments.dropFirst().first ?? "30") ?? 30
// 需要"输入监控"权限（设备标了 RequiresTCCAuthorization）；首次运行会请求
if !IOHIDRequestAccess(kIOHIDRequestTypeListenEvent) { print("input monitoring not granted"); exit(2) }
let mgr = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(mgr, [kIOHIDVendorIDKey: 0x056A, kIOHIDProductIDKey: 0x48EE] as CFDictionary)

var counts: [UInt32: Int] = [:]
let cb: IOHIDReportCallback = { _, result, _, _, reportID, report, length in
    counts[reportID, default: 0] += 1
    let bytes = UnsafeBufferPointer(start: report, count: length).map { String(format: "%02x", $0) }.joined(separator: " ")
    print(String(format: "%.3f", Date().timeIntervalSince1970), "rid=\(reportID) len=\(length) \(bytes)")
    fflush(stdout)
}

IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetCurrent(), CFRunLoopMode.defaultMode.rawValue)
let r = IOHIDManagerOpen(mgr, IOOptionBits(kIOHIDOptionsTypeNone))
print("open: 0x" + String(UInt32(bitPattern: r), radix: 16), "access(listen)=\(IOHIDCheckAccess(kIOHIDRequestTypeListenEvent).rawValue)")
guard let devs = IOHIDManagerCopyDevices(mgr) as? Set<IOHIDDevice>, !devs.isEmpty else { print("no device"); exit(1) }
var buffers: [UnsafeMutablePointer<UInt8>] = []
for d in devs {
    let buf = UnsafeMutablePointer<UInt8>.allocate(capacity: 512)
    buffers.append(buf)
    IOHIDDeviceRegisterInputReportCallback(d, buf, 512, cb, nil)
    print("device:", IOHIDDeviceGetProperty(d, kIOHIDProductKey as CFString) ?? "?",
          "usage", IOHIDDeviceGetProperty(d, kIOHIDPrimaryUsagePageKey as CFString) ?? "?",
          IOHIDDeviceGetProperty(d, kIOHIDPrimaryUsageKey as CFString) ?? "?")
}
CFRunLoopRunInMode(.defaultMode, seconds, false)
print("summary:", counts.sorted { $0.key < $1.key }.map { "rid \($0.key): \($0.value)" }.joined(separator: ", "))
