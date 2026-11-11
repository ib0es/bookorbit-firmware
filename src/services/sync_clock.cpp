#include "sync_clock.h"
#include "time_utils.h"
#include "../hal/rtc.h"
#include "../platform/system.h"
#include <cstdlib>

namespace {
// Anything before 2023-01-01 means "never set" (ESP32 boots at 1970).
constexpr time_t kMinValidUtc = 1672531200;
}

namespace SyncClock {

uint32_t nowUtc() {
    const time_t t = Platform::systemTime();
    return (t >= kMinValidUtc) ? (uint32_t)t : 0;
}

bool isSet() { return nowUtc() != 0; }

void initFromRtc() {
    uint32_t rtcUtc = 0;
    if (!BoardRtc.readUtc(rtcUtc) || rtcUtc < (uint32_t)kMinValidUtc) return;
    Platform::setSystemTime((time_t)rtcUtc);
    Serial.printf("[Clock] Restored from RTC: %lu\n", (unsigned long)rtcUtc);
}

void setFromServer(uint32_t utc) {
    if (utc < (uint32_t)kMinValidUtc) return;
    if (BoardRtc.present()) {
        uint32_t rtcUtc = 0;
        const bool rtcOk = BoardRtc.readUtc(rtcUtc);
        const long rtcDrift = rtcOk ? (long)utc - (long)rtcUtc : 999L;
        if (!rtcOk || std::abs(rtcDrift) >= 2) {
            BoardRtc.writeUtc(utc);
        }
    }
    time_t now = (time_t)nowUtc();
    long drift = now ? ((long)utc - (long)now) : 999L;
    if (drift > -2 && drift < 2) return;
    Platform::setSystemTime((time_t)utc);
    Serial.printf("[Clock] Set from server: %lu (drift %ld s)\n", (unsigned long)utc, drift);
}

void setFromHttpDate(const String& dateHeader) {
    if (dateHeader.length() == 0) return;
    setFromServer(TimeUtils::parseHttpDate(dateHeader.c_str()));
}

int32_t currentLocalMinute(int utcOffsetMin) {
    const uint32_t utc = nowUtc();
    if (utc == 0) return -1;
    const int64_t localSec = (int64_t)utc + (int64_t)TimeUtils::clampUtcOffset(utcOffsetMin) * 60;
    return (int32_t)(localSec / 60);
}

bool formatLocalTime(int utcOffsetMin, char* buf, size_t len) {
    const uint32_t utc = nowUtc();
    if (utc == 0) {
        if (buf && len > 0) buf[0] = '\0';
        return false;
    }
    return TimeUtils::formatClockHHMM((int64_t)utc, utcOffsetMin, buf, len);
}

}  // namespace SyncClock
