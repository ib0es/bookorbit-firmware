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

// Whether the new book engine (FreeInkBook, FREEINK_BOOK_SMALL) would fit now:
// tries to allocate its two buffers for laying out a chapter (layout 50 KB, parse
// 46 KB, each in one piece) and the open book (30 KB in 2 KB pieces), frees them
// and logs "[Mem] engine probe <where>: layout 50 KB ok, parse 46 KB ok, book 30 KB ok".
// Returns true if all fit.
bool probeEngine(const char* where);

// The heap's biggest pieces: "[Mem] map <where>: free blocks 63 11 7 4 KB (+N smaller),
// used blocks 47 23 12 KB (+N smaller)". Shows what fragments the heap and who
// holds the large allocations (diagnostics for the move to the new engine).
void logMap(const char* where);

// One line for the device info screen.
String summary();

}  // namespace MemStats
