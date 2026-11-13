#pragma once
#include "../memory_tier.h"

// ESP32-C3, no PSRAM. After boot the heap's biggest free block is ~96 KB and
// the next ~12 KB; with a book open and Wi-Fi up, free memory falls to ~12-25 KB.
inline constexpr MemoryTier kSmallMemory = {
        /*name*/ "small",
        /*catalogMaxBooks*/ 200,
        /*coversInRam*/ 1,
        /*wifiTakesReaderMemory*/ true,
        /*restartWhenOutOfMemory*/ true,
        /*indexScratchMax*/ 96 * 1024,
        /*layoutExtraMax*/ 32 * 1024,
        /*pageRecordMax*/ 64 * 1024,
};
