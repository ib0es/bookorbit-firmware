#pragma once

#include <Arduino.h>
#include <vector>
#include <SDCardManager.h>
#include <ArduinoJson.h>

struct AppConfig;

struct LastBookInfo {
    String filePath;
    String title;
    String author;
    String coverPath;
    float progress = 0.0f;
    int page = 1;
    int totalPages = 1;
    int fileId = 0; // BookOrbit server fileId
    String docId; // KOReader partial MD5 hash
    String xpath; // KOReader progress xpath
    // Sync state (dirty flag, last agreed position, timestamps) lives in
    // ProgressStore (/books/progress.json); this struct is the UI's "current book".
};

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
    bool isAvailable() const;

    uint64_t totalBytes() const;
    uint64_t usedBytes();

    // Configuration file (/config.json). load() only overwrites fields present in the
    // file, so defaults from AppConfig survive a partial config.
    bool loadConfig(AppConfig& cfg);
    bool saveConfig(const AppConfig& cfg);

    // Directory & file helpers
    bool ensureDirectories();
    FsFile open(const char* path, oflag_t oflag = O_RDONLY);
    bool exists(const char* path);
    bool remove(const char* path);
    bool rename(const char* path, const char* newPath);
    bool replaceFile(const char* tmpPath, const char* path);
    bool mkdir(const char* path);
    bool removeDir(const char* path);   // with everything in it
    // Names of the sub-directories of a directory, unsorted.
    std::vector<String> listSubdirs(const String& path);

    // Scan for available books on SD card
    std::vector<String> listBooks();
    std::vector<DirEntry> listDirectory(const String& path);
    // Names of all plain files in a directory, unfiltered and unsorted.
    std::vector<String> listFileNames(const String& path);

    // Catalog cache (/books/catalog.json) helpers
    bool saveCatalog(const std::vector<struct BookMetadata>& catalog);
    bool loadCatalog(std::vector<struct BookMetadata>& outCatalog);
    bool removeCatalogBook(const String& id, const String& title, const String& filePath);

    // Last opened book state (/books/last_book.json)
    bool saveLastBook(const LastBookInfo& info);
    bool loadLastBook(LastBookInfo& outInfo);
};

extern StorageManager Storage;

// Parses a JSON file straight from the card: no copy of it in a String and no
// size limit (SDCardManager::readFile stops at 50 KB, which cut a large catalog
// or progress file and made it unreadable). EmptyInput when the file is missing.
DeserializationError readJsonFile(const char* path, JsonDocument& doc);
// Moves an unreadable JSON file aside ("<path>.bad"), so that the next save does
// not write an empty list over data that may still be recovered by hand.
void keepUnreadableFile(const char* path);
