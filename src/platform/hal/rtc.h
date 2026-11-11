#pragma once

// Hardware RTC wrapper (DS3231 / PCF8563 / RX8130 via freeink::Rtc).
// Stores UTC so timezone changes do not require rewriting hardware registers.

#include <Arduino.h>
#include <Rtc.h>

class RtcHelper {
public:
    bool begin();
    bool present() const { return _rtc.present(); }
    bool lastReadValid() const { return _lastReadOk; }

    bool readUtc(uint32_t& outUtc);
    bool writeUtc(uint32_t utc);

private:
    freeink::Rtc _rtc;
    bool _lastReadOk = false;
};

extern RtcHelper BoardRtc;
