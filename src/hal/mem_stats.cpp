#include "mem_stats.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace MemStats {

Snapshot now() {
    Snapshot s;
#if defined(ESP_PLATFORM)
    s.freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.minFreeEver = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
#else
    s.freeBytes = s.largestBlock = s.minFreeEver = ESP.getFreeHeap();
#endif
    return s;
}

void log(const char* where) {
    const Snapshot s = now();
    Serial.printf("[Mem] %s: free %u KB, largest block %u KB, min since boot %u KB\n", where,
                  (unsigned)(s.freeBytes / 1024), (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
}

String summary() {
    const Snapshot s = now();
    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %u KB (blk %u, min %u)", (unsigned)(s.freeBytes / 1024),
             (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
    return String(buf);
}

}  // namespace MemStats
