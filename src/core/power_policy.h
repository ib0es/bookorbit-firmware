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

// USB mass storage with no computer on the other end (the cable pulled
// without "exit", or a charger) keeps the device awake to the last percent:
// after as long as the idle timeout, the mode is left and the usual sleep
// rules apply again (B8). `noHostMs` counts from when no host was seen.
inline bool shouldLeaveUsbMsc(bool hostConnected, uint32_t noHostMs, uint32_t sleepTimeoutMs) {
    return !hostConnected && noHostMs >= sleepTimeoutMs;
}

}  // namespace PowerPolicy
