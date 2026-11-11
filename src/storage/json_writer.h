#pragma once

// ArduinoJson writer over a card file (or anything with write(buf, n)), in
// 512-byte blocks: a document goes to the card without a String copy of it.
// The serializer stops at the first refused byte, and the count of what was
// accepted tells a complete file from a cut one (JsonFile::write checks it
// against measureJson).
//
// Pure (no Arduino): host-tested in test/host/test_json_writer.cpp.

#include <cstddef>
#include <cstdint>
#include <cstring>

template <class File>
class JsonFileWriter {
public:
    explicit JsonFileWriter(File& f) : _f(f) {}
    ~JsonFileWriter() { flush(); }

    size_t write(uint8_t c) { return write(&c, 1); }
    size_t write(const uint8_t* s, size_t len) {
        size_t done = 0;
        while (done < len && !_failed) {
            size_t k = len - done;
            if (k > sizeof(_buf) - _n) k = sizeof(_buf) - _n;
            memcpy(_buf + _n, s + done, k);
            _n += k;
            done += k;
            if (_n == sizeof(_buf)) flush();
        }
        _accepted += done;
        return done;
    }

    // Writes what is buffered; false once any write fell short.
    bool flush() {
        if (_n > 0 && !_failed) {
            if ((size_t)_f.write(_buf, _n) != _n) _failed = true;
        }
        _n = 0;
        return !_failed;
    }

    bool failed() const { return _failed; }
    size_t accepted() const { return _accepted; }

private:
    File& _f;
    uint8_t _buf[512];
    size_t _n = 0;
    size_t _accepted = 0;
    bool _failed = false;
};
