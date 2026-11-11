#pragma once

// Simulator RTC stub matching freeink::Rtc.
// Headless tests can set SIM_RTC_UTC=<epoch> (valid RTC) or SIM_RTC_OSF=1
// (RTC present with oscillator-stopped invalid time until set() is called).

#include <cstdint>
#include <cstdlib>
#include <ctime>

namespace freeink {

class Rtc {
public:
    struct DateTime {
        uint16_t year = 2000;
        uint8_t month = 1;
        uint8_t day = 1;
        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        uint8_t weekday = 0;
    };

    bool begin() {
        const char* utcEnv = std::getenv("SIM_RTC_UTC");
        const char* osfEnv = std::getenv("SIM_RTC_OSF");
        if (utcEnv && *utcEnv) {
            begun_ = true;
            hasValid_ = true;
            epoch_ = (uint32_t)std::strtoul(utcEnv, nullptr, 10);
            return true;
        }
        if (osfEnv && *osfEnv == '1') {
            begun_ = true;
            hasValid_ = false;
            epoch_ = 0;
            return true;
        }
        begun_ = false;
        return false;
    }

    bool present() const { return begun_; }

    bool now(DateTime& out) {
        if (!begun_ || !hasValid_) return false;
        time_t t = (time_t)epoch_;
        struct tm tmUtc{};
#if defined(_WIN32)
        gmtime_s(&tmUtc, &t);
#else
        gmtime_r(&t, &tmUtc);
#endif
        out.year = (uint16_t)(tmUtc.tm_year + 1900);
        out.month = (uint8_t)(tmUtc.tm_mon + 1);
        out.day = (uint8_t)tmUtc.tm_mday;
        out.hour = (uint8_t)tmUtc.tm_hour;
        out.minute = (uint8_t)tmUtc.tm_min;
        out.second = (uint8_t)tmUtc.tm_sec;
        out.weekday = (uint8_t)tmUtc.tm_wday;
        return true;
    }

    bool set(const DateTime& dt) {
        if (!begun_) return false;
        struct tm tmUtc{};
        tmUtc.tm_year = dt.year - 1900;
        tmUtc.tm_mon = dt.month - 1;
        tmUtc.tm_mday = dt.day;
        tmUtc.tm_hour = dt.hour;
        tmUtc.tm_min = dt.minute;
        tmUtc.tm_sec = dt.second;
#if defined(_WIN32)
        time_t t = _mkgmtime(&tmUtc);
#else
        time_t t = timegm(&tmUtc);
#endif
        if (t <= 0) return false;
        epoch_ = (uint32_t)t;
        hasValid_ = true;
        return true;
    }

    bool adjust(int32_t seconds, DateTime* out = nullptr) {
        if (!begun_ || !hasValid_) return false;
        epoch_ = (uint32_t)((int64_t)epoch_ + seconds);
        return out ? now(*out) : true;
    }

private:
    bool begun_ = false;
    bool hasValid_ = false;
    uint32_t epoch_ = 0;
};

}  // namespace freeink

using Rtc = freeink::Rtc;
