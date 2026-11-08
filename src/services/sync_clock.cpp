#include "sync_clock.h"
#include "time_utils.h"
#include <time.h>
#include <sys/time.h>

namespace {
// Anything before 2023-01-01 means "never set" (ESP32 boots at 1970).
constexpr time_t kMinValidUtc = 1672531200;
}

namespace SyncClock {

uint32_t nowUtc() {
    time_t t = time(nullptr);
    return (t >= kMinValidUtc) ? (uint32_t)t : 0;
}

bool isSet() { return nowUtc() != 0; }

void setFromServer(uint32_t utc) {
    if (utc < (uint32_t)kMinValidUtc) return;
    time_t now = time(nullptr);
    long drift = (long)utc - (long)now;
    if (drift > -2 && drift < 2) return;
#if defined(ESP_PLATFORM)
    struct timeval tv = { (time_t)utc, 0 };
    settimeofday(&tv, nullptr);
    Serial.printf("[Clock] Set from server: %lu (drift %ld s)\n", (unsigned long)utc, drift);
#else
    (void)drift;   // desktop simulator: the host clock is already correct
#endif
}

void setFromHttpDate(const String& dateHeader) {
    if (dateHeader.length() == 0) return;
    setFromServer(TimeUtils::parseHttpDate(dateHeader.c_str()));
}

}  // namespace SyncClock
