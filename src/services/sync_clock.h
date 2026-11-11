#pragma once

// Wall-clock time for progress sync and status-bar clock. Set from a hardware
// RTC at boot (when present) and synchronized from Book Orbit HTTP "Date" headers.

#include <Arduino.h>

namespace SyncClock {

// Current UTC seconds, or 0 if the clock has never been set.
uint32_t nowUtc();

bool isSet();

// Seeds system time from the hardware RTC (if present and OSF is clear).
void initFromRtc();

// Sets the clock from a server timestamp (UTC seconds); ignores invalid timestamps
// and writes through to hardware RTC if present and drifted.
void setFromServer(uint32_t utc);

// Convenience: parse an HTTP Date header value and set the clock from it.
void setFromHttpDate(const String& dateHeader);

// Returns minutes since epoch in local time (or -1 if clock unset), used to detect minute ticks.
int32_t currentLocalMinute(int utcOffsetMin);

// Formats local "HH:MM" into buf; returns false if clock is unset.
bool formatLocalTime(int utcOffsetMin, char* buf, size_t len);

}  // namespace SyncClock
