#pragma once
#include "../memory_tier.h"

// ESP32-S3 with 8 MB PSRAM. Estimates until checked on a Picco (see memory_tier.h).
inline constexpr MemoryTier kLargeMemory = {
        /*name*/ "large",
        /*catalogMaxBooks*/ 2000,
        /*coversInRam*/ 4,
        /*wifiTakesReaderMemory*/ false,
        /*restartWhenOutOfMemory*/ false,
        /*indexScratchMax*/ 384 * 1024,
        /*layoutExtraMax*/ 128 * 1024,
        /*pageRecordMax*/ 256 * 1024,
};
