#pragma once

// Heap diagnostics. The ESP32-C3 has no PSRAM: with a book open and Wi-Fi on the
// free heap can drop to ~15 KB, so "free" alone says little; the largest free
// block (what one malloc can get) and the lowest free value since boot matter.

#include <Arduino.h>

namespace MemStats {

struct Snapshot {
    uint32_t freeBytes = 0;     // free heap now
    uint32_t largestBlock = 0;  // biggest single allocation possible now
    uint32_t minFreeEver = 0;   // lowest free heap since boot
};

Snapshot now();

// "[Mem] <where>: free 48 KB, largest block 30 KB, min since boot 12 KB"
void log(const char* where);

// One line for the device info screen.
String summary();

}  // namespace MemStats
