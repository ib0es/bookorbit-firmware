// The ESP32 side of platform/font_partition.h: the "spiffs" data partition.
#include "../font_partition.h"

#include <algorithm>
#include <esp_partition.h>

namespace FontPartition {

namespace {
const esp_partition_t* partition() {
    static const esp_partition_t* p =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
    return p;
}
esp_partition_mmap_handle_t s_map = 0;
}  // namespace

uint32_t capacity() {
    const esp_partition_t* p = partition();
    return p ? p->size : 0;
}

bool read(uint32_t offset, void* dst, uint32_t len) {
    const esp_partition_t* p = partition();
    return p && esp_partition_read(p, offset, dst, len) == ESP_OK;
}

bool erase(uint32_t bytes, void (*progress)(void*, int), void* user) {
    const esp_partition_t* p = partition();
    if (!p) return false;
    // The header goes first (erased: no family), then the faces.
    const uint32_t eraseBytes = (bytes + 4095) & ~4095u;
    if (esp_partition_erase_range(p, 0, 4096) != ESP_OK) return false;
    for (uint32_t e = 4096; e < eraseBytes; e += 64 * 1024) {
        const uint32_t n = std::min<uint32_t>(64 * 1024, eraseBytes - e);
        if (esp_partition_erase_range(p, e, n) != ESP_OK) return false;
        if (progress) progress(user, (int)(300ull * e / eraseBytes));
    }
    return true;
}

bool write(uint32_t offset, const void* src, uint32_t len) {
    const esp_partition_t* p = partition();
    return p && esp_partition_write(p, offset, src, len) == ESP_OK;
}

bool finish() { return true; }

const uint8_t* map(uint32_t bytes) {
    const void* ptr = nullptr;
    if (esp_partition_mmap(partition(), 0, bytes, ESP_PARTITION_MMAP_DATA, &ptr, &s_map) != ESP_OK) return nullptr;
    return static_cast<const uint8_t*>(ptr);
}

void unmap() {
    esp_partition_munmap(s_map);
    s_map = 0;
}

}  // namespace FontPartition
