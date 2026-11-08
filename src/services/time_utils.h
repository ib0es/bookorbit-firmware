#pragma once

// UTC time parsing for server timestamps (pure, host-tested).

#include <cstdint>

namespace TimeUtils {

// Seconds since 1970-01-01T00:00:00Z for a civil UTC date-time (no time zone, no mktime).
int64_t utcFromCivil(int year, int month, int day, int hour, int minute, int second);

// ISO 8601 / RFC 3339: "2026-11-07T10:15:30Z", "...30.123Z", "...30+03:00",
// "2026-11-07 10:15:30" (no zone = UTC). Returns 0 if it cannot be parsed.
uint32_t parseIso8601(const char* s);

// RFC 7231 HTTP date: "Sat, 07 Nov 2026 10:15:30 GMT". Returns 0 on failure.
uint32_t parseHttpDate(const char* s);

}  // namespace TimeUtils
