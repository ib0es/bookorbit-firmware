#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "Arduino.h"

namespace fs = std::filesystem;

typedef int oflag_t;
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR   0x02
#define O_CREAT  0x0100
#define O_TRUNC  0x0200
#ifndef O_WRITE
#define O_WRITE  O_WRONLY
#endif

class FsFile {
public:
    FsFile() : _f(nullptr), _size(0), _isDir(false), _dirIterIndex(0) {}
    FsFile(FILE* f, size_t sz, bool isDir = false, const std::string& name = "") 
        : _f(f), _size(sz), _isDir(isDir), _name(name), _dirIterIndex(0) {}
    ~FsFile() = default;

    explicit operator bool() const { return _f != nullptr || _isDir; }

    bool isDirectory() const { return _isDir; }

    void getName(char* buf, size_t sz) const {
        if (!buf || sz == 0) return;
        strncpy(buf, _name.c_str(), sz - 1);
        buf[sz - 1] = '\0';
    }

    bool openNext(FsFile* dir, oflag_t oflag = O_RDONLY) {
        if (!dir || !dir->_isDir) return false;
        if (dir->_dirIterIndex >= dir->_dirEntries.size()) return false;
        *this = dir->_dirEntries[dir->_dirIterIndex++];
        return true;
    }

    size_t size() const { return _size; }

    int available() const {
        return (curPosition() < size()) ? (int)(size() - curPosition()) : 0;
    }

    size_t readBytesUntil(char terminator, char* buffer, size_t length) {
        if (!_f || !buffer || length == 0) return 0;
        size_t count = 0;
        while (count < length && available()) {
            int c = fgetc(_f);
            if (c == EOF) break;
            if ((char)c == terminator) break;
            buffer[count++] = (char)c;
        }
        buffer[count] = '\0';
        return count;
    }

    uint32_t curPosition() const {
        if (!_f) return 0;
        return (uint32_t)ftell(_f);
    }

    uint32_t position() const {
        return curPosition();
    }

    template <typename... Args>
    void printf(const char* fmt, Args... args) {
        if (_f) fprintf(_f, fmt, args...);
    }

    bool seek(uint32_t pos) {
        if (!_f) return false;
        return fseek(_f, (long)pos, SEEK_SET) == 0;
    }

    size_t read(void* buf, size_t count) {
        if (!_f || !buf || count == 0) return 0;
        return fread(buf, 1, count, _f);
    }

    size_t readBytes(char* buf, size_t count) {
        return read(buf, count);
    }

    bool preAllocate(uint32_t) { return false; }
    bool truncate(uint32_t) { return true; }
    size_t write(const void* buf, size_t count) {
        if (!_f || !buf || count == 0) return 0;
        size_t n = fwrite(buf, 1, count, _f);
        long cur = ftell(_f);
        if (cur > (long)_size) _size = (size_t)cur;
        return n;
    }

    size_t write(uint8_t b) {
        return write(&b, 1);
    }

    void flush() {
        if (_f) fflush(_f);
    }

    void sync() {
        flush();
    }

    void close() {
        if (_f) {
            fclose(_f);
            _f = nullptr;
        }
        _isDir = false;
        _dirEntries.clear();
        _dirIterIndex = 0;
    }

    void populateDirEntries(const std::vector<FsFile>& entries) {
        _dirEntries = entries;
        _dirIterIndex = 0;
    }

private:
    FILE* _f;
    size_t _size;
    bool _isDir;
    std::string _name;
    std::vector<FsFile> _dirEntries;
    size_t _dirIterIndex;
};

struct BoardConfig_SD {
    int powerEnable;
    bool powerActiveHigh;
    uint32_t spiHz;
};

struct BoardConfig_Input {
    int8_t back;
    int8_t confirm;
    int8_t left;
    int8_t right;
    int8_t up;
    int8_t down;
    int8_t power;
    bool powerActiveHigh;
};

struct BoardConfig_Type {
    BoardConfig_SD sd;
    BoardConfig_Input input;
};

struct BoardConfig {
#if defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
    static inline BoardConfig_Type ACTIVE = { { 6, false, 40000000u }, { 9, 8, 5, 2, 0, 7, 3, false } };
#else
    static inline BoardConfig_Type ACTIVE = { { 13, true, 40000000u }, { -1, -1, -1, -1, 0, 7, 3, false } };
#endif
    static void holdPowerRails() {}
    static void releaseSdRail() {}
    static bool hasHomeKey() {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
        return true;
#else
        return false;
#endif
    }
};

class FsBlockDeviceInterface {};

class SDCardManager {
public:
    static SDCardManager& getInstance() {
        static SDCardManager instance;
        return instance;
    }

    void setRoot(const std::string& path) {
        _rootDir = path;
    }

    const std::string& getRoot() const {
        return _rootDir;
    }

    std::string resolvePath(const char* path) const {
        std::string p = path ? path : "";
        while (!p.empty() && p[0] == '/') p.erase(0, 1);
        return _rootDir + "/" + p;
    }

    bool begin() { _detached = false; return true; }
    bool ready() const { return !_detached; }
    void shutdown() { _detached = true; }
    FsBlockDeviceInterface* detachFilesystemForRawAccess() {
        if (_detached) return nullptr;
        _detached = true;
        static FsBlockDeviceInterface s_simBlockDev;
        return &s_simBlockDev;
    }
    uint64_t sdTotalBytes() const { return (uint64_t)16 * 1024 * 1024 * 1024; }

    bool ensureDirectoryExists(const char* path) {
        try {
            fs::create_directories(resolvePath(path));
            return true;
        } catch (...) {
            return false;
        }
    }

    std::vector<String> listFiles(const char* path, size_t maxFiles = 200) {
        std::vector<String> res;
        try {
            std::string realPath = resolvePath(path);
            if (fs::exists(realPath) && fs::is_directory(realPath)) {
                for (const auto& entry : fs::directory_iterator(realPath)) {
                    if (res.size() >= maxFiles) break;
                    res.push_back(String(entry.path().filename().string()));
                }
            }
        } catch (...) {}
        return res;
    }

    FsFile open(const char* path, oflag_t oflag = O_RDONLY) {
        std::string realPath = resolvePath(path);
        try {
            if (fs::exists(realPath) && fs::is_directory(realPath)) {
                std::vector<FsFile> entries;
                for (const auto& it : fs::directory_iterator(realPath)) {
                    bool isSubDir = it.is_directory();
                    size_t sz = isSubDir ? 0 : it.file_size();
                    entries.emplace_back(nullptr, sz, isSubDir, it.path().filename().string());
                }
                FsFile dirFile(nullptr, 0, true, fs::path(realPath).filename().string());
                dirFile.populateDirEntries(entries);
                return dirFile;
            }
        } catch (...) {}

        const char* mode = "rb";
        if ((oflag & O_CREAT) || (oflag & O_TRUNC) || (oflag & O_WRONLY)) {
            // Ensure parent directory exists
            try {
                fs::path p(realPath);
                if (p.has_parent_path()) {
                    fs::create_directories(p.parent_path());
                }
            } catch (...) {}
            mode = (oflag & O_RDWR) ? "w+b" : "wb";
        } else if (oflag & O_RDWR) {
            mode = "r+b";
        }

        FILE* f = fopen(realPath.c_str(), mode);
        if (!f) return FsFile();

        fseek(f, 0, SEEK_END);
        size_t sz = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);

        return FsFile(f, sz);
    }

    bool exists(const char* path) {
        std::string realPath = resolvePath(path);
        return fs::exists(realPath);
    }

    bool remove(const char* path) {
        try {
            return fs::remove(resolvePath(path));
        } catch (...) {
            return false;
        }
    }

    bool rename(const char* path, const char* newPath) {
        try {
            fs::rename(resolvePath(path), resolvePath(newPath));
            return true;
        } catch (...) {
            return false;
        }
    }

    bool replaceFile(const char* tmpPath, const char* path) {
        remove(path);
        return rename(tmpPath, path);
    }

    bool removeDir(const char* path) {
        try {
            return fs::remove_all(resolvePath(path)) > 0;
        } catch (...) {
            return false;
        }
    }

    bool mkdir(const char* path) {
        return ensureDirectoryExists(path);
    }

    bool writeFile(const char* path, const String& content) {
        FsFile f = open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (!f) return false;
        f.write(content.c_str(), content.length());
        f.close();
        return true;
    }

    String readFile(const char* path) {
        FsFile f = open(path, O_RDONLY);
        if (!f) return "";
        size_t sz = f.size();
        if (sz == 0) { f.close(); return ""; }
        std::string buf(sz, '\0');
        f.read(&buf[0], sz);
        f.close();
        return String(buf);
    }

private:
    SDCardManager() : _rootDir("./sdcard") {}
    std::string _rootDir;
    bool _detached = false;
};
