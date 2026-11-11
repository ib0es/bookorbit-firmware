#pragma once

// FreeInkBook storage adapters over the firmware's SD card (Storage HAL):
// the book file as a random-access BookSource, and a per-book cache directory
// (layout pages, container index) as CacheStorage.

#include <Arduino.h>
#include <BookStorage.h>

#include "../../platform/hal/storage.h"

class SdBookSource : public freeink::book::BookSource {
public:
    bool open(const String& path);
    void close();
    bool isOpen() const { return _open; }
    int32_t readAt(uint64_t offset, void* dst, uint32_t len) override;
    uint64_t size() const override { return _size; }

private:
    FsFile _file;
    uint64_t _size = 0;
    uint64_t _pos = UINT64_MAX;   // file position after the last read (skips needless seeks)
    bool _open = false;
};

// One directory per book. A file is written as "<name>.tmp" and renamed on
// endWrite(), so a power cut never leaves a torn cache file.
class SdCacheStorage : public freeink::book::CacheStorage {
public:
    void setDir(const String& dir) { _dir = dir; }
    const String& dir() const { return _dir; }

    bool exists(const char* name) override;
    bool remove(const char* name) override;
    int64_t fileSize(const char* name) override;
    int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override;
    bool beginWrite(const char* name) override;
    bool write(const void* data, uint32_t len) override;
    bool endWrite() override;
    int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override;

    // Drops the file being written (its .tmp) without replacing the old one.
    void abortWrite();

    // Removes every file of the directory (the directory stays).
    void clear();

private:
    String path(const char* name) const { return _dir + "/" + name; }

    String _dir;
    // The last file read stays open: a page turn reads one cache file a few times.
    FsFile _read;
    String _readName;
    FsFile _write;
    String _writeName;
    uint32_t _writePos = 0;
};
