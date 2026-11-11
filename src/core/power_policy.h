#pragma once

#include <cstdint>

namespace PowerPolicy {

inline bool shouldBlockIdleDeepSleep(bool usbMscActive, bool charging, bool usbSerialConnected) {
    return usbMscActive || charging || usbSerialConnected;
}

inline bool shouldEnterIdleDeepSleep(bool usbMscActive, bool charging, bool usbSerialConnected,
                                     uint32_t idleMs, uint32_t sleepTimeoutMs) {
    if (shouldBlockIdleDeepSleep(usbMscActive, charging, usbSerialConnected)) return false;
    return idleMs >= sleepTimeoutMs;
}

}  // namespace PowerPolicy
