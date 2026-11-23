// SPDX-License-Identifier: BSD-3-Clause
import Foundation

enum PowerKeyAction: Equatable {
    case ignore
    case wakeDisplayOnly
    case showPanel
}

func decidePowerKeyAction(
    now: Date,
    lastPress: Date,
    lastAsleep: Date,
    unlockSuppressUntil: Date,
    controlHeld: Bool,
    sessionLocked: Bool,
    debounce: TimeInterval = 3.0
) -> (action: PowerKeyAction, updateLastPress: Bool) {
    if controlHeld {
        return (.ignore, true)
    }
    if now.timeIntervalSince(lastPress) < debounce {
        return (.ignore, false)
    }
    if now.timeIntervalSince(lastAsleep) < 1.5 {
        return (.wakeDisplayOnly, true)
    }
    if sessionLocked || now < unlockSuppressUntil {
        return (.ignore, true)
    }
    return (.showPanel, true)
}
