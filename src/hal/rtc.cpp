#include "rtc.h"
#include "../services/time_utils.h"

namespace {
constexpr uint32_t kMinValidUtc = 1672531200UL;  // 2023-01-01T00:00:00Z
}

RtcHelper BoardRtc;

bool RtcHelper::begin() {
    const bool ok = _rtc.begin();
    if (!ok) {
        _lastReadOk = false;
        Serial.println("[RTC] Not detected or unsupported on this board");
        return false;
    }
    uint32_t utc = 0;
    if (readUtc(utc)) {
        Serial.printf("[RTC] Ready (UTC %lu)\n", (unsigned long)utc);
    } else {
        Serial.println("[RTC] Present, time invalid/stopped (OSF)");
    }
    return true;
}

bool RtcHelper::readUtc(uint32_t& outUtc) {
    outUtc = 0;
    if (!_rtc.present()) {
        _lastReadOk = false;
        return false;
    }
    freeink::Rtc::DateTime dt;
    if (!_rtc.now(dt)) {
        _lastReadOk = false;
        return false;
    }
    const int64_t t = TimeUtils::utcFromCivil(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    if (t < (int64_t)kMinValidUtc || t > 0xFFFFFFFFLL) {
        _lastReadOk = false;
        return false;
    }
    outUtc = (uint32_t)t;
    _lastReadOk = true;
    return true;
}

bool RtcHelper::writeUtc(uint32_t utc) {
    if (!_rtc.present() || utc < kMinValidUtc) return false;
    const TimeUtils::CivilDateTime c = TimeUtils::civilFromUtc((int64_t)utc);
    freeink::Rtc::DateTime dt;
    dt.year = (uint16_t)c.year;
    dt.month = (uint8_t)c.month;
    dt.day = (uint8_t)c.day;
    dt.hour = (uint8_t)c.hour;
    dt.minute = (uint8_t)c.minute;
    dt.second = (uint8_t)c.second;
    dt.weekday = (uint8_t)c.weekday;
    const bool ok = _rtc.set(dt);
    if (ok) {
        _lastReadOk = true;
        Serial.printf("[RTC] Updated from server: %04d-%02d-%02d %02d:%02d:%02dZ\n",
                      c.year, c.month, c.day, c.hour, c.minute, c.second);
    }
    return ok;
}
