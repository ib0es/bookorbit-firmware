#include "wifi_session.h"

#include "../platform/device.h"
#include "../reader/reader.h"
#include <Arduino.h>

namespace WifiSession {

void prepare() {
    if (!Platform::kMemory.wifiTakesReaderMemory) {
        Serial.println("[WiFi] The reader keeps its memory (large memory tier).");
        return;
    }
    Serial.println("[WiFi] Reader memory given up for the radio: background layout stopped, font caches dropped.");
    Reader.stopBackground();
    Reader.releaseFontCaches();
}

}
