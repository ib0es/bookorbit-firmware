#include "zip_writer.h"

#include <string.h>

namespace convert {

namespace {
uint32_t g_crcTable[256];
bool g_crcReady = false;

void crcInit() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crcTable[i] = c;
    }
    g_crcReady = true;
}

// Fixed timestamp (1 Jan 2020): the content, not the moment, defines the file.
constexpr uint16_t kDosTime = 0;
constexpr uint16_t kDosDate = ((2020 - 1980) << 9) | (1 << 5) | 1;
}  // namespace

uint32_t crc32Update(uint32_t crc, const void* data, uint32_t len) {
    if (!g_crcReady) crcInit();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len--) crc = g_crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void ZipWriter::begin(OutFile* out) {
    _out = out;
    _entries.clear();
    _pos = out ? out->tell() : 0;
    _open = false;
    _failed = out == nullptr;
}

bool ZipWriter::raw(const void* data, uint32_t len) {
    if (_failed) return false;
    if (len && !_out->write(data, len)) {
        _failed = true;
        return false;
    }
    _pos += len;
    return true;
}

bool ZipWriter::u16(uint16_t v) {
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    return raw(b, 2);
}

bool ZipWriter::u32(uint32_t v) {
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return raw(b, 4);
}

bool ZipWriter::open(const char* name) {
    if (_failed) return false;
    if (_open && !close()) return false;
    Entry e;
    e.name = name;
    e.offset = _pos;
    _entries.push_back(e);
    const uint16_t nameLen = (uint16_t)strlen(name);
    u32(0x04034b50);
    u16(20);         // version needed
    u16(0x0800);     // UTF-8 names
    u16(0);          // stored
    u16(kDosTime);
    u16(kDosDate);
    u32(0);          // crc, sizes: patched by close()
    u32(0);
    u32(0);
    u16(nameLen);
    u16(0);
    raw(name, nameLen);
    _open = !_failed;
    return _open;
}

bool ZipWriter::write(const void* data, uint32_t len) {
    if (!_open || _failed) return false;
    Entry& e = _entries.back();
    e.crc = crc32Update(e.crc, data, len);
    e.size += len;
    return raw(data, len);
}

bool ZipWriter::close() {
    if (!_open) return !_failed;
    _open = false;
    if (_failed) return false;
    const Entry& e = _entries.back();
    const uint32_t end = _pos;
    uint8_t b[12];
    const uint32_t v[3] = {e.crc, e.size, e.size};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++) b[i * 4 + k] = (uint8_t)(v[i] >> (8 * k));
    if (!_out->seek(e.offset + 14) || !_out->write(b, sizeof(b)) || !_out->seek(end)) {
        _failed = true;
        return false;
    }
    return true;
}

bool ZipWriter::add(const char* name, const void* data, uint32_t len) {
    return open(name) && write(data, len) && close();
}

bool ZipWriter::add(const char* name, const char* text) { return add(name, text, (uint32_t)strlen(text)); }

bool ZipWriter::finish() {
    if (_open && !close()) return false;
    const uint32_t cdStart = _pos;
    for (const Entry& e : _entries) {
        u32(0x02014b50);
        u16(20);
        u16(20);
        u16(0x0800);
        u16(0);
        u16(kDosTime);
        u16(kDosDate);
        u32(e.crc);
        u32(e.size);
        u32(e.size);
        u16((uint16_t)e.name.size());
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(e.offset);
        raw(e.name.data(), (uint32_t)e.name.size());
    }
    const uint32_t cdSize = _pos - cdStart;
    u32(0x06054b50);
    u16(0);
    u16(0);
    u16((uint16_t)_entries.size());
    u16((uint16_t)_entries.size());
    u32(cdSize);
    u32(cdStart);
    u16(0);
    return !_failed;
}

}  // namespace convert
