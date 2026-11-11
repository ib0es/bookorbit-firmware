#pragma once
#include "../memory_tier.h"

// ESP32-S3 with 8 MB PSRAM. Estimates until checked on a Picco (see memory_tier.h).
inline constexpr MemoryTier kLargeMemory = {
    /*name*/ "large",
    /*catalogMaxBooks*/ 200,
    /*coversInRam*/ 1,
    /*wifiTakesReaderMemory*/ true,
    /*restartWhenOutOfMemory*/ true,
    /*indexScratchMax*/ 96 * 1024,
    /*layoutExtraMax*/ 32 * 1024,
    /*pageRecordMax*/ 64 * 1024,
};
