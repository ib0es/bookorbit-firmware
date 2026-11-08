#pragma once

#include <Arduino.h>
#include <vector>
#include "../hal/storage.h"
#include "progress_store.h"

struct BookMetadata;

// Library item tracking (local files + remote catalog)
struct BookItem {
    String title;
    String author;
    String filePath;     // Local path e.g. "/books/Frankenstein.epub" or folder path "/books/Фантастика"
    String downloadUrl;  // Book Orbit OPDS download URL (empty if purely local)
    String coverUrl;     // Remote cover thumbnail URL
    String coverPath;    // Local cached cover path e.g. "/covers/Frankenstein.jpg"
    int fileId = 0;      // Server file id (progress uploads)
    int bookId = 0;      // Server book id (progress downloads)
    float progress = 0.0f; // 0.0 to 1.0: this device's position if the book was opened here, else the server's
    int page = 1;
    String docId;        // KOReader document MD5 hash
    String xpath;        // KOReader progress XPath
    String series;       // Series name e.g. "The Wheel of Time"
    int seriesIndex = 0; // Series index e.g. 1
    int16_t libraryIdx = -1;  // server library: index into LibraryManager::libraryNames (-1 = none)
    int subItemCount = 0;// Child count for virtual folder (for badge)
    bool isDownloaded = false;   // true if file exists on SD card
    bool isDirectory = false;    // true if item is a folder
};

enum LibraryFilter {
    FILTER_ALL,
    FILTER_SEARCH,
    FILTER_LIBRARIES,
    FILTER_AUTHORS,
    FILTER_SERIES,
    FILTER_FOLDERS,
    FILTER_DOWNLOADED,
    FILTER_COMPLETED,
    FILTER_CLOUD
};

struct RecentBookInfo {
    String title;
    String author;
    String filePath;
    String coverPath;
    String downloadUrl;
    int fileId = 0;
    float progress = 0.0f;
    int page = 1;
    int totalPages = 1;
    String docId;
    String xpath;
    bool isDownloaded = true;
};

class LibraryManager {
public:
    static LibraryManager& getInstance() {
        static LibraryManager instance;
        return instance;
    }

    // Every book once: files under /books merged with the saved catalog.
    std::vector<BookItem> masterBooks;
    // Names of the server libraries (BookItem::libraryIdx), each stored once.
    std::vector<String> libraryNames;
    // The current list (library screen): rows are indices into masterBooks, or
    // -1-i for folderRows[i] (virtual folders, "..", SD folders, loose files).
    std::vector<int> rows;
    std::vector<BookItem> folderRows;
    int rowCount() const;
    BookItem& rowAt(int i);            // a reference: changes go to the master entry
    int rowMasterIndex(int i) const;   // -1 for a folder row
    int selectedBookIndex = 0;
    int savedAuthorIndex = 0;
    int savedSeriesIndex = 0;
    int savedLibraryIndex = 0;
    String currentFolder = "/books";
    LibraryFilter currentFilter = FILTER_ALL;
    String searchQuery = "";
    int recentBookIndex = 0;
    LastBookInfo lastBook;

    // Builds the list of currentFolder / currentFilter. The master list is
    // (re)built when empty or forced (after a sync); otherwise only the
    // positions are refreshed from progress.json.
    void scanLocalBooks(bool forceMasterScan = false);
    // Rebuilds the list (and counts) after the master list changed in place.
    void rebuildFilteredIndices();
    // Picks up books copied to / removed from the card since the master scan.
    void refreshLocalFiles();
    std::vector<RecentBookInfo> getRecentBooks();

    // Catalog counters, kept up to date by scanLocalBooks / rebuildFilteredIndices
    // / setHideCompleted (the catalog screen redraws often).
    struct Counts {
        int total = 0, libraries = 0, authors = 0, series = 0, downloaded = 0, completed = 0, folders = 0;
    };
    Counts counts;
    bool hideCompleted = false;   // mirrors AppConfig::hideCompleted
    void setHideCompleted(bool hide);

    const String& libraryOf(const BookItem& b) const;

    // Case-insensitive UTF-8 helpers (ASCII + Cyrillic), no allocation except search.
    static uint32_t nextCodepoint(const char*& p);
    static uint32_t foldCodepoint(uint32_t cp);
    static int foldCompare(const String& a, const String& b);
    static bool utf8ContainsIgnoreCase(const String& haystack, const String& needle);

    // A cover the renderer can draw: JPEG (baseline) or BMP; anything else is deleted.
    static bool isValidCoverFile(const String& path);
    static bool isProgressiveJpeg(const String& path);
    static String resolveCoverPath(const String& filePath, const String& title, const String& explicitCoverPath = "", bool allowExtract = false);
    static void deleteBookArtifacts(const BookItem& book, bool removeCover = true);
    // Removes /cache and /covers files that belong to no book: neither a book
    // file anywhere on the SD card nor a catalog entry (covers only). Leftovers
    // of books deleted before deleteBookArtifacts cleaned everything up.
    void cleanOrphanFiles();
    void updateLastBookAfterDeletion(const String& deletedPath);

    void initLastBook();
    bool saveLastBook();        // last_book.json + progress.json
    bool loadLastBook();

    // ---- progress of the current book (lastBook) ----
    // Sync record of lastBook in ProgressStore (created if missing).
    BookProgressRecord& currentRecord();
    // The user moved in the open book (page turn, chapter jump): updates lastBook and
    // marks the record dirty. `withXPath` also computes the KOReader xpath (reads the
    // SD card, so page turns skip it). `persist` writes both files.
    void recordReaderMove(bool withXPath, bool persist);
    // Not a user move (closing, sleeping, font change, before a sync): refresh
    // page/xpath of lastBook and its record from the open reader, then save.
    void flushReaderPosition();
    // Copies a record's local position into lastBook (if it is the current book)
    // and into the matching library item.
    void applyRecordToLibrary(const BookProgressRecord& rec);

    // File name used for a catalog book on the SD card ("/books/<safe title>.epub").
    static String safeFileName(const String& title, const String& fallbackId);
    static String catalogBookPath(const BookMetadata& b);
    // Server ids of a catalog entry: book id (from the OPDS id) and file id.
    static int catalogBookId(const BookMetadata& b);

private:
    LibraryManager() = default;
    void buildMaster();
    void refreshProgress();
    void refreshCounts();
    void rebuildRows();
    int internLibrary(const String& name);
};
