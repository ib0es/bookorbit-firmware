#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>

// Two independent catalog sectors at the END of the partition. Legacy FIBF
// blobs at offset zero remain readable and can be adopted without moving data.
namespace FontCatalog {
constexpr uint32_t kSector = 4096;
constexpr uint32_t kReserved = 2 * kSector;
constexpr uint32_t kMax = 32;
constexpr uint32_t kMagic = 0x32424946u; // FIB2
struct Face {
    uint32_t offset, length;
};
struct Header {
    uint32_t magic, version;
    char name[64];
    Face faces[4];
    uint32_t checksum;
};
struct Record {
    uint32_t offset, span;
};
struct Directory {
    uint32_t magic, version, generation, count;
    Record records[kMax];
    uint32_t checksum;
};
static_assert(sizeof(Header) == 108 && sizeof(Directory) == 276, "Flash format layout");
inline uint32_t checksum(const void* bytes, size_t length) {
    uint32_t value = 2166136261u;
    const auto* p = static_cast<const uint8_t*>(bytes);
    while (length--) value = (value ^ *p++) * 16777619u;
    return value;
}
inline uint32_t aligned(uint32_t value) { return (value + kSector - 1) / kSector * kSector; }
inline bool valid(const Header& h, uint32_t span) {
    if (h.magic != 0x46424946u || h.version != 1 || !h.name[0] || !memchr(h.name, 0, 64) ||
        h.checksum != checksum(&h, offsetof(Header, checksum)) || !h.faces[0].length)
        return false;
    for (const auto& f : h.faces)
        if (f.length && (f.offset < kSector || (uint64_t)f.offset + f.length > span)) return false;
    return true;
}
inline uint32_t span(const Header& h) {
    uint32_t end = kSector;
    for (const auto& f : h.faces)
        if (f.length) end = std::max(end, f.offset + f.length);
    return aligned(end);
}
inline bool valid(const Directory& d, uint32_t limit) {
    if (d.magic != kMagic || d.version != 2 || d.count > kMax ||
        d.checksum != checksum(&d, offsetof(Directory, checksum)))
        return false;
    for (uint32_t i = 0; i < d.count; i++) {
        const auto& r = d.records[i];
        if (r.offset % kSector || r.span < 2 * kSector || r.span % kSector || (uint64_t)r.offset + r.span > limit)
            return false;
        for (uint32_t j = 0; j < i; j++) {
            const auto& other = d.records[j];
            if (r.offset < other.offset + other.span && other.offset < r.offset + r.span) return false;
        }
    }
    return true;
}
inline bool newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
inline uint32_t findSpace(const Directory& d, uint32_t limit, uint32_t need) {
    uint32_t at = 0;
    while ((uint64_t)at + need <= limit) {
        uint32_t next = at;
        for (uint32_t i = 0; i < d.count; i++) {
            const auto& r = d.records[i];
            if (at < r.offset + r.span && r.offset < at + need) next = std::max(next, r.offset + r.span);
        }
        if (next == at) return at;
        at = next;
    }
    return UINT32_MAX;
}
inline uint32_t largestFree(const Directory& d, uint32_t limit) {
    uint32_t best = 0, at = 0;
    while (at < limit) {
        uint32_t next = limit, occupiedEnd = at;
        for (uint32_t i = 0; i < d.count; i++) {
            const auto& r = d.records[i];
            if (r.offset <= at && r.offset + r.span > at)
                occupiedEnd = std::max(occupiedEnd, r.offset + r.span);
            else if (r.offset > at)
                next = std::min(next, r.offset);
        }
        if (occupiedEnd > at)
            at = occupiedEnd;
        else {
            best = std::max(best, next - at);
            at = next;
        }
    }
    return best;
}
} // namespace FontCatalog
