#include "time_utils.h"

#include <cctype>
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
    return y >= 1970 && y < 2106 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 &&
           h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 60;
}

static uint32_t clampEpoch(int64_t t) {
    return (t > 0 && t <= 0xFFFFFFFFLL) ? (uint32_t)t : 0;
}

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
        if (strncmp(p, kMonths[i], 3) == 0) { mo = i + 1; break; }
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
