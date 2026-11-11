#pragma once

// The flash partition an installed typeface is copied to (reader/font_store):
// on the ESP32 the "spiffs" data partition, memory-mapped when read; in the
// simulator a file on the card. Offsets count from the partition's start.

#include <cstdint>

namespace FontPartition {

uint32_t capacity();   // bytes in all (0: no partition)
bool read(uint32_t offset, void* dst, uint32_t len);
// Clears the first `bytes` for writing: the first 4 KB (the header) first, then
// the rest, reporting 0..300 permille of the whole install as it goes.
bool erase(uint32_t bytes, void (*progress)(void*, int), void* user);
bool write(uint32_t offset, const void* src, uint32_t len);   // erased area only
bool finish();         // after the last write
// The first `bytes` in memory (read-only) until unmap(); nullptr on failure.
const uint8_t* map(uint32_t bytes);
void unmap();

}  // namespace FontPartition
