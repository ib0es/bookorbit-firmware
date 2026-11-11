#include "wifi_session.h"

#include "sync_service.h"
#include "../net/bookorbit_client.h"
#include "../platform/device.h"
#include "../platform/hal/mem_stats.h"
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

void giveMemoryToReader() {
    if (!Platform::kMemory.wifiTakesReaderMemory) return;
    const bool upload = SyncService::backgroundActive();
    if (upload) SyncService::cancelBackground();
    if (!upload && !BookOrbit.isConnected()) return;
    Serial.println("[WiFi] Radio off: the book needs the memory.");
    BookOrbit.disconnectWiFi();
    waitForHeapToSettle();
}

void waitForHeapToSettle(uint32_t maxMs) {
    const uint32_t t0 = millis();
    uint32_t last = MemStats::now().freeBytes;
    int still = 0;
    while (millis() - t0 < maxMs && still < 2) {
        delay(50);
        const uint32_t now = MemStats::now().freeBytes;
        still = now > last ? 0 : still + 1;
        if (now > last) last = now;
    }
    MemStats::log("heap settled after Wi-Fi off");
}

}
