#pragma once

// Wall-clock time for progress sync. The X4 has no RTC battery and no NTP, so the
// clock is set from the HTTP "Date" header of Book Orbit responses. On the ESP32
// the system time keeps running through deep sleep, so it stays valid until the
// battery is removed or the device is reset.

#include <Arduino.h>

namespace SyncClock {

// Current UTC seconds, or 0 if the clock has never been set.
uint32_t nowUtc();

bool isSet();

// Sets the clock from a server timestamp (UTC seconds); ignores 0.
void setFromServer(uint32_t utc);

// Convenience: parse an HTTP Date header value and set the clock from it.
void setFromHttpDate(const String& dateHeader);

}  // namespace SyncClock
