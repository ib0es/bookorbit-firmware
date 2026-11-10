#pragma once
// A ZIP (EPUB container) writer for stored entries: no compression, so no
// deflate state in RAM; sizes and CRC are patched into each local header when
// the entry ends. Central directory records are kept in RAM until finish()
// (name + 12 bytes per entry).
#include <string>
#include <vector>

#include "convert_io.h"

namespace convert {

uint32_t crc32Update(uint32_t crc, const void* data, uint32_t len);   // crc starts at 0

class ZipWriter {
public:
    void begin(OutFile* out);
    bool add(const char* name, const void* data, uint32_t len);   // a whole small entry
    bool add(const char* name, const char* text);
    bool open(const char* name);                                 // then write()... close()
    bool write(const void* data, uint32_t len);
    bool close();
    bool isOpen() const { return _open; }
    bool finish();                                               // central directory
    bool failed() const { return _failed; }
    uint32_t bytesWritten() const { return _pos; }

private:
    struct Entry {
        std::string name;
        uint32_t offset = 0;
        uint32_t crc = 0;
        uint32_t size = 0;
    };
    bool raw(const void* data, uint32_t len);
    bool u16(uint16_t v);
    bool u32(uint32_t v);

    OutFile* _out = nullptr;
    std::vector<Entry> _entries;
    uint32_t _pos = 0;
    bool _open = false;
    bool _failed = false;
};

}  // namespace convert
