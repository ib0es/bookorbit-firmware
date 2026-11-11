#pragma once

// What the firmware may assume about memory, by class of chip. Two classes:
// small (ESP32-C3, ~400 KB of internal RAM, no PSRAM: X4, X3) and large
// (ESP32-S3 with 8 MB PSRAM: X4 Classic, X4 Pro, Picco). Each device profile
// names its tier (platform/devices/*.h); code reads Platform::kMemory.
//
// On the S3 the Arduino-ESP32 build allocates with SPIRAM_USE_MALLOC: blocks
// over 4 KB come from PSRAM, so the reader's buffers, the catalog and the
// cover cache do not compete with Wi-Fi for internal RAM. The large-tier
// numbers rest on that and are estimates until measured on a Picco.
//
// The base sizes of the reader's buffers and the layout profile are the same
// in both tiers, so page caches stay valid when a card moves between devices;
// only how far a retry may grow differs.

#include <cstddef>
#include <cstdint>

struct MemoryTier {
    const char* name;

    // ---- catalog
    uint16_t catalogMaxBooks;        // books kept from the OPDS feed

    // ---- covers
    uint8_t coversInRam;             // 1-bit thumbnails kept decoded (home carousel)

    // ---- Wi-Fi
    // Wi-Fi needs the memory of a background layout and of the font caches:
    // both are given up before the radio comes on.
    bool wifiTakesReaderMemory;

    // ---- reader
    // A book that fails to open for want of memory (heap cut up after Wi-Fi)
    // restarts the device and opens again.
    bool restartWhenOutOfMemory;
    size_t indexScratchMax;          // container index: retried up to this
    size_t layoutExtraMax;           // chapter layout: retried up to base + this
    size_t pageRecordMax;            // one page's runs and text: retried up to this
};
