#pragma once
// Files a book converter works with, behind small interfaces so the converters
// run the same on the card (SdFat) and in host tests (stdio).
#include <stddef.h>
#include <stdint.h>

namespace convert {

// The source book, read from the start (possibly several times).
class InFile {
public:
    virtual ~InFile() = default;
    virtual bool rewind() = 0;
    virtual int32_t read(void* dst, uint32_t len) = 0;   // 0 at the end, <0 on error
    virtual uint64_t size() const = 0;
};

// The EPUB being written: sequential, with seeks back to patch ZIP headers.
class OutFile {
public:
    virtual ~OutFile() = default;
    virtual bool write(const void* data, uint32_t len) = 0;
    virtual bool seek(uint32_t pos) = 0;
    virtual uint32_t tell() = 0;
};

// A scratch file: written once, then read back from the start.
class TempFile {
public:
    virtual ~TempFile() = default;
    virtual bool beginWrite() = 0;
    virtual bool write(const void* data, uint32_t len) = 0;
    virtual bool beginRead() = 0;
    virtual int32_t read(void* dst, uint32_t len) = 0;
};

// Progress of a conversion: 0..1000.
using ProgressFn = void (*)(void* user, int permille);

}  // namespace convert
