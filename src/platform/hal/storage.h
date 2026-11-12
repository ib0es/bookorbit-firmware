#pragma once

#include <Arduino.h>
#include <vector>
#include <SDCardManager.h>

struct DirEntry {
    String name;
    String fullPath;
    bool isDirectory;
    size_t size;
};

class StorageManager {
public:
    StorageManager();

    bool begin();
    void shutdown();
    FsBlockDeviceInterface* detachForUsbMsc();
    bool isAvailable() const;

    uint64_t totalBytes() const;
    uint64_t usedBytes();
    uint64_t freeBytes();

    // Directory & file helpers
    bool ensureDirectories();
    bool wipeAll();
    FsFile open(const char* path, oflag_t oflag = O_RDONLY);
    bool exists(const char* path);
    bool remove(const char* path);
    bool rename(const char* path, const char* newPath);
    bool replaceFile(const char* tmpPath, const char* path);
    bool mkdir(const char* path);
    bool removeDir(const char* path);   // with everything in it
    // Names of the sub-directories of a directory, unsorted.
    std::vector<String> listSubdirs(const String& path);

    std::vector<DirEntry> listDirectory(const String& path);
    // Names of all plain files in a directory, unfiltered and unsorted.
    std::vector<String> listFileNames(const String& path);
};

extern StorageManager Storage;
