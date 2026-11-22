// SPDX-License-Identifier: BSD-3-Clause
// Unit tests for PowerKeyPolicy.swift (6 cases covering unlocked panel,
// display-off wake-only, awake+locked suppression, 2.5s unlock cooldown,
// Control-held pass-through, and 3.0s debounce).

import Foundation

@main
struct PowerKeyPolicyTests {
    static func check(_ condition: @autoclosure () -> Bool, _ msg: String, line: Int = #line) {
        if !condition() {
            FileHandle.standardError.write("FAIL (line \(line)): \(msg)\n".data(using: .utf8)!)
            exit(1)
        }
    }

    static func main() {
        let t0 = Date(timeIntervalSince1970: 1_700_000_000)

        // 1. Unlocked + display awake (now - lastAsleep == 10.0) -> .showPanel
        let c1 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-10.0),
            lastAsleep: t0.addingTimeInterval(-10.0),
            unlockSuppressUntil: t0.addingTimeInterval(-5.0),
            controlHeld: false,
            sessionLocked: false
        )
        check(c1.action == .showPanel && c1.updateLastPress == true, "case 1 expected .showPanel")

        // 2. Display asleep/just woke (now - lastAsleep == 0.8) -> .wakeDisplayOnly (even when locked)
        let c2 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-10.0),
            lastAsleep: t0.addingTimeInterval(-0.8),
            unlockSuppressUntil: t0.addingTimeInterval(-5.0),
            controlHeld: false,
            sessionLocked: true
        )
        check(c2.action == .wakeDisplayOnly && c2.updateLastPress == true, "case 2 expected .wakeDisplayOnly")

        // 3. Display awake + sessionLocked == true (now - lastAsleep == 10.0) -> .ignore
        let c3 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-10.0),
            lastAsleep: t0.addingTimeInterval(-10.0),
            unlockSuppressUntil: t0.addingTimeInterval(-5.0),
            controlHeld: false,
            sessionLocked: true
        )
        check(c3.action == .ignore && c3.updateLastPress == true, "case 3 expected .ignore while locked")

        // 4. Display awake + sessionLocked == false + now < unlockSuppressUntil (2.5s cooldown) -> .ignore
        let c4 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-10.0),
            lastAsleep: t0.addingTimeInterval(-10.0),
            unlockSuppressUntil: t0.addingTimeInterval(2.5),
            controlHeld: false,
            sessionLocked: false
        )
        check(c4.action == .ignore && c4.updateLastPress == true, "case 4 expected .ignore during unlock cooldown")

        // 5. controlHeld == true -> .ignore with updateLastPress == true
        let c5 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-10.0),
            lastAsleep: t0.addingTimeInterval(-10.0),
            unlockSuppressUntil: t0.addingTimeInterval(-5.0),
            controlHeld: true,
            sessionLocked: false
        )
        check(c5.action == .ignore && c5.updateLastPress == true, "case 5 expected .ignore with updateLastPress=true")

        // 6. Press within debounce (1.0s < 3.0s) -> .ignore with updateLastPress == false
        let c6 = decidePowerKeyAction(
            now: t0,
            lastPress: t0.addingTimeInterval(-1.0),
            lastAsleep: t0.addingTimeInterval(-10.0),
            unlockSuppressUntil: t0.addingTimeInterval(-5.0),
            controlHeld: false,
            sessionLocked: false,
            debounce: 3.0
        )
        check(c6.action == .ignore && c6.updateLastPress == false, "case 6 expected .ignore with updateLastPress=false")

        print("All 6 PowerKeyPolicy tests passed")
    }
}
