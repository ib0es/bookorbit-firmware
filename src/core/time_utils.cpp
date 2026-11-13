#include "time_utils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace TimeUtils {

int64_t utcFromCivil(int y, int m, int d, int hh, int mm, int ss) {
    // Howard Hinnant's days_from_civil.
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + (int64_t)doe - 719468;
    return days * 86400 + hh * 3600 + mm * 60 + ss;
}

CivilDateTime civilFromUtc(int64_t utc) {
    // Howard Hinnant's civil_from_days paired with Euclidean time-of-day split.
    int64_t days = utc / 86400;
    int64_t rem = utc % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    int wd = (int)((days + 4) % 7);
    if (wd < 0) wd += 7;
    return CivilDateTime{
            (int)(y + (m <= 2)), (int)m, (int)d, (int)(rem / 3600), (int)((rem % 3600) / 60), (int)(rem % 60), wd,
    };
}

int clampUtcOffset(int offsetMin) {
    if (offsetMin < -720) return -720;
    if (offsetMin > 840) return 840;
    return offsetMin;
}

int stepUtcOffset(int offsetMin, int dir) {
    constexpr int kMin = -720;
    constexpr int kMax = 840;
    constexpr int kStep = 30;
    constexpr int kCount = (kMax - kMin) / kStep + 1;  // 53 values: -12:00..+14:00
    const int clamped = clampUtcOffset(offsetMin);
    int idx = (clamped - kMin + (kStep / 2)) / kStep;
    idx = (idx + (dir > 0 ? 1 : kCount - 1)) % kCount;
    return kMin + idx * kStep;
}

void formatUtcOffset(int offsetMin, char* buf, size_t len) {
    if (!buf || len == 0) return;
    offsetMin = clampUtcOffset(offsetMin);
    if (offsetMin == 0) {
        snprintf(buf, len, "UTC+0");
        return;
    }
    const char sign = offsetMin < 0 ? '-' : '+';
    const int absM = offsetMin < 0 ? -offsetMin : offsetMin;
    const int h = absM / 60;
    const int m = absM % 60;
    if (m == 0) {
        snprintf(buf, len, "UTC%c%d", sign, h);
    } else {
        snprintf(buf, len, "UTC%c%d:%02d", sign, h, m);
    }
}

bool formatClockHHMM(int64_t utc, int utcOffsetMin, char* buf, size_t len) {
    if (!buf || len == 0) return false;
    buf[0] = '\0';
    if (utc <= 0) return false;
    const int64_t localSec = utc + (int64_t)clampUtcOffset(utcOffsetMin) * 60;
    const CivilDateTime c = civilFromUtc(localSec);
    snprintf(buf, len, "%02d:%02d", c.hour, c.minute);
    return true;
}

static bool readInt(const char*& p, int digits, int& out) {
    out = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit((unsigned char)p[i])) return false;
        out = out * 10 + (p[i] - '0');
    }
    p += digits;
    return true;
}

static bool validDate(int y, int mo, int d, int h, int mi, int s) {
    return y >= 1970 && y < 2106 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 && mi >= 0 &&
           mi <= 59 && s >= 0 && s <= 60;
}

static uint32_t clampEpoch(int64_t t) { return (t > 0 && t <= 0xFFFFFFFFLL) ? (uint32_t)t : 0; }

uint32_t parseIso8601(const char* s) {
    if (!s) return 0;
    const char* p = s;
    int y, mo, d, h, mi, sec;
    if (!readInt(p, 4, y) || *p++ != '-' || !readInt(p, 2, mo) || *p++ != '-' || !readInt(p, 2, d)) return 0;
    if (*p != 'T' && *p != 't' && *p != ' ') return 0;
    p++;
    if (!readInt(p, 2, h) || *p++ != ':' || !readInt(p, 2, mi)) return 0;
    sec = 0;
    if (*p == ':') {
        p++;
        if (!readInt(p, 2, sec)) return 0;
    }
    if (*p == '.' || *p == ',') {  // fractional seconds, ignored
        p++;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (!validDate(y, mo, d, h, mi, sec)) return 0;

    int64_t t = utcFromCivil(y, mo, d, h, mi, sec);
    if (*p == '+' || *p == '-') {
        const int sign = (*p == '+') ? 1 : -1;
        p++;
        int oh, om = 0;
        if (!readInt(p, 2, oh)) return 0;
        if (*p == ':') p++;
        if (isdigit((unsigned char)*p) && !readInt(p, 2, om)) return 0;
        t -= sign * (oh * 3600 + om * 60);   // local = UTC + offset
    } else if (*p == 'Z' || *p == 'z') {
        p++;
    }
    if (*p != '\0') return 0;   // trailing garbage
    return clampEpoch(t);
}

uint32_t parseHttpDate(const char* s) {
    if (!s) return 0;
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char* comma = strchr(s, ',');
    const char* p = comma ? comma + 1 : s;
    while (*p == ' ') p++;
    int d, y, h, mi, sec;
    // Day: one or two digits.
    if (!isdigit((unsigned char)*p)) return 0;
    d = *p++ - '0';
    if (isdigit((unsigned char)*p)) d = d * 10 + (*p++ - '0');
    while (*p == ' ') p++;
    int mo = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(p, kMonths[i], 3) == 0) {
            mo = i + 1;
            break;
        }
    }
    if (mo == 0) return 0;
    p += 3;
    while (*p == ' ') p++;
    if (!readInt(p, 4, y)) return 0;
    while (*p == ' ') p++;
    if (!readInt(p, 2, h) || *p++ != ':' || !readInt(p, 2, mi) || *p++ != ':' || !readInt(p, 2, sec)) return 0;
    if (!validDate(y, mo, d, h, mi, sec)) return 0;
    return clampEpoch(utcFromCivil(y, mo, d, h, mi, sec));
}

}  // namespace TimeUtils
