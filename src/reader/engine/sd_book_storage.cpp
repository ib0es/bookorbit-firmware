#include "sd_book_storage.h"

// ---------------------------------------------------------------- SdBookSource

bool SdBookSource::open(const String& path) {
    close();
    _file = Storage.open(path.c_str(), O_RDONLY);
    if (!_file) return false;
    _size = _file.size();
    _pos = 0;
    _open = true;
    return true;
}

void SdBookSource::close() {
    if (_open) _file.close();
    _open = false;
    _size = 0;
    _pos = UINT64_MAX;
}

int32_t SdBookSource::readAt(uint64_t offset, void* dst, uint32_t len) {
    if (!_open) return -1;
    if (offset >= _size) return 0;
    if (offset != _pos && !_file.seek((uint32_t)offset)) return -1;
    const int n = _file.read((uint8_t*)dst, len);
    if (n < 0) { _pos = UINT64_MAX; return -1; }
    _pos = offset + (uint32_t)n;
    return n;
}

// -------------------------------------------------------------- SdCacheStorage

bool SdCacheStorage::exists(const char* name) { return Storage.exists(path(name).c_str()); }

bool SdCacheStorage::remove(const char* name) {
    if (_readName == name) { _read.close(); _readName = ""; }
    return Storage.remove(path(name).c_str());
}

int64_t SdCacheStorage::fileSize(const char* name) {
    if (_readName == name && _read) return (int64_t)_read.size();
    FsFile f = Storage.open(path(name).c_str(), O_RDONLY);
    if (!f) return -1;
    const int64_t n = (int64_t)f.size();
    f.close();
    return n;
}

int32_t SdCacheStorage::readAt(const char* name, uint32_t offset, void* dst, uint32_t len) {
    if (_readName != name || !_read) {
        if (_read) _read.close();
        _read = Storage.open(path(name).c_str(), O_RDONLY);
        if (!_read) { _readName = ""; return -1; }
        _readName = name;
    }
    if (!_read.seek(offset)) return -1;
    return _read.read((uint8_t*)dst, len);
}

bool SdCacheStorage::beginWrite(const char* name) {
    if (_write) _write.close();
    _writeName = name;
    const String tmp = path(name) + ".tmp";
    Storage.remove(tmp.c_str());
    _write = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    _writePos = 0;
    return (bool)_write;
}

bool SdCacheStorage::write(const void* data, uint32_t len) {
    if (!_write) return false;
    if (_write.write((const uint8_t*)data, len) != len) return false;
    _writePos += len;
    return true;
}

bool SdCacheStorage::endWrite() {
    if (!_write) return false;
    _write.close();
    if (_readName == _writeName) { _read.close(); _readName = ""; }
    const String tmp = path(_writeName.c_str()) + ".tmp";
    const bool ok = Storage.replaceFile(tmp.c_str(), path(_writeName.c_str()).c_str());
    _writeName = "";
    return ok;
}

int32_t SdCacheStorage::readBackAt(uint32_t offset, void* dst, uint32_t len) {
    if (!_write) return -1;
    if (!_write.seek(offset)) return -1;
    const int n = _write.read((uint8_t*)dst, len);
    _write.seek(_writePos);
    return n;
}

void SdCacheStorage::clear() {
    if (_read) _read.close();
    _readName = "";
    for (const String& name : Storage.listFileNames(_dir)) Storage.remove(path(name.c_str()).c_str());
}
