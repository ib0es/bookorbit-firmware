#pragma once

#include <cstdint>

namespace LightSleepPolicy {

struct Inputs {
    bool featureBuilt = false;       // FREEINK_GT911_INT_WAKE == 1
    bool settingEnabled = false;     // ctx.config.touchWakeEnabled
    int8_t touchWakeIrqPin = -1;     // Buttons.touchWakeIrqPin()
    bool wifiOff = true;             // WiFi.getMode() == WIFI_OFF
    bool syncBgActive = false;       // SyncService::backgroundActive()
    bool readerBgActive = false;     // Reader.backgroundActive()
    bool usbMscActive = false;       // UsbMsc.active()
    bool debouncePending = false;
    bool touchPressed = false;
    bool homeKeyDown = false;
    bool usbSerialConnected = false; // Keep CDC Serial debug responsive when host attached
    bool irqAlreadyAsserted = false; // Guard against level-triggered immediate wakeup loop
    uint32_t idleMs = 0;             // millis() - ctx.lastActivityTime
};

inline bool canEnter(const Inputs& in) {
    if (!in.featureBuilt || !in.settingEnabled || in.touchWakeIrqPin < 0) return false;
    if (!in.wifiOff || in.syncBgActive || in.readerBgActive || in.usbMscActive) return false;
    if (in.debouncePending || in.touchPressed || in.homeKeyDown) return false;
    if (in.usbSerialConnected || in.irqAlreadyAsserted) return false;
    if (in.idleMs < 500) return false;
    return true;
}

struct WakeTiming {
    bool readingClockActive = false; // STATE_READING && sbClock && !Modal.isActive()
    uint32_t msToNextMinute = 60000;
    bool waitingPrelayout = false;   // STATE_READING && !Modal.isActive() && idleMs < prelayoutDelayMs
    uint32_t msToPrelayout = 0;
    bool onBattery = true;
    uint32_t idleMs = 0;
    uint32_t sleepTimeoutMs = 300000;
    uint32_t batteryCheckIntervalMs = 60000;
    uint32_t msSinceBatteryCheck = 0;
    uint32_t modalRemainingMs = 0;   // 0 when no timed modal is active
    uint32_t maxSleepMs = 15000;
};

inline uint32_t nextWakeDelayMs(const WakeTiming& t) {
    uint32_t wait = t.maxSleepMs > 0 ? t.maxSleepMs : 15000;
    if (t.onBattery) {
        const uint32_t toIdleSleep = (t.idleMs >= t.sleepTimeoutMs) ? 0 : (t.sleepTimeoutMs - t.idleMs);
        if (toIdleSleep < wait) wait = toIdleSleep;
    }
    const uint32_t toBattery = (t.msSinceBatteryCheck >= t.batteryCheckIntervalMs)
                                       ? 0
                                       : (t.batteryCheckIntervalMs - t.msSinceBatteryCheck);
    if (toBattery < wait) wait = toBattery;
    if (t.modalRemainingMs > 0 && t.modalRemainingMs < wait) wait = t.modalRemainingMs;
    if (t.waitingPrelayout && t.msToPrelayout < wait) wait = t.msToPrelayout;
    if (t.readingClockActive && t.msToNextMinute < wait) wait = t.msToNextMinute;
    return wait;
}

}  // namespace LightSleepPolicy
