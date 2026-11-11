#pragma once

// UTC time parsing, calendar conversions, and clock formatting (pure, host-tested).

#include <cstddef>
#include <cstdint>

namespace TimeUtils {

struct CivilDateTime {
    int year;
    int month;    // 1..12
    int day;      // 1..31
    int hour;     // 0..23
    int minute;   // 0..59
    int second;   // 0..59
    int weekday;  // 0=Sunday..6=Saturday
};

// Seconds since 1970-01-01T00:00:00Z for a civil UTC date-time (no time zone, no mktime).
int64_t utcFromCivil(int year, int month, int day, int hour, int minute, int second);

// Civil UTC date-time and weekday from UTC seconds (inverse of utcFromCivil, no TZ dependency).
CivilDateTime civilFromUtc(int64_t utc);

// Format local "HH:MM" from UTC epoch + UTC offset minutes (returns false when utc <= 0).
bool formatClockHHMM(int64_t utc, int utcOffsetMin, char* buf, size_t len);

// Format UTC offset minutes as "UTC+0", "UTC+3", "UTC-5", "UTC+5:30", etc.
void formatUtcOffset(int offsetMin, char* buf, size_t len);

// Clamp UTC offset minutes to [-12h, +14h] (-720..+840).
int clampUtcOffset(int offsetMin);

// Step UTC offset by 30 minutes forward (dir > 0) or backward (dir < 0), wrapping over [-720, +840].
int stepUtcOffset(int offsetMin, int dir);

// ISO 8601 / RFC 3339: "2026-11-07T10:15:30Z", "...30.123Z", "...30+03:00",
// "2026-11-07 10:15:30" (no zone = UTC). Returns 0 if it cannot be parsed.
uint32_t parseIso8601(const char* s);

// RFC 7231 HTTP date: "Sat, 07 Nov 2026 10:15:30 GMT". Returns 0 on failure.
uint32_t parseHttpDate(const char* s);

}  // namespace TimeUtils
