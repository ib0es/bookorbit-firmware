#pragma once

#include <Arduino.h>
#include <vector>
#include "../platform/hal/storage.h"
#include "../storage/book_records.h"
#include "progress_store.h"


// One format (file) of a book: "/books/<name>.epub", "/books/<name>.fb2"... next
// to each other, each with its own server file id and reading position.
struct BookFormatSlot {
    int fileId = 0;       // server file id, 0 for a file only on the card
    uint8_t kind = 0;     // BookFormats::Kind
    bool onCard = false;
    String url;           // download address, "" = canonical (BookFormats::compactUrl)
};

// Library item tracking (local files + remote catalog)
struct BookItem {
    String title;
    String author;
    // The active format (formats[activeFormat]): filePath, fileId and isDownloaded
    // are its own, so opening, syncing and deleting work on one file.
    String filePath;     // Local path e.g. "/books/Frankenstein.epub" or folder path "/books/Фантастика"
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
    std::vector<BookFormatSlot> formats;   // every format; empty for folders
    int8_t activeFormat = -1;
    int8_t formatRow = -1;       // a row of the format list: index into the book's formats

    // Book Orbit download address of the active format ("" = only on the card).
    String downloadUrl() const;
    String downloadUrlOf(const BookFormatSlot& f) const;
    bool inCatalog() const;      // some format is on the server
    bool anyOnCard() const;      // some format is on the card
    uint8_t activeKind() const { return activeFormat >= 0 && activeFormat < (int)formats.size() ? formats[activeFormat].kind : 0; }
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
    String currentFolder = "/books";   // also "@formats": the formats of masterBooks[formatsBook]
    int formatsBook = -1;
    String formatsReturnFolder;
    int formatsReturnIndex = 0;
    LibraryFilter currentFilter = FILTER_ALL;
    String searchQuery = "";
    int recentBookIndex = 0;
    LastBookInfo lastBook;

    // Builds the list of currentFolder / currentFilter. The master list is
    // (re)built when empty or forced (after a sync); otherwise only the
    // positions are refreshed from progress.json.
    void scanLocalBooks(bool forceMasterScan = false);
    // The first scan, if none was made yet (a wake-up straight into a book skips it).
    void ensureScanned() { if (!_scanned) scanLocalBooks(); }
    // Rebuilds the list (and counts) after the master list changed in place.
    void rebuildFilteredIndices();
    // Picks up books copied to / removed from the card since the master scan.
    void refreshLocalFiles();
    // Books downloaded by firmware before November 2026 are named by their title
    // alone: renamed to their catalogStem, with the cache, cover, bookmarks and
    // position records that go by the name. Run on the saved catalog when the
    // list is built, and on a fresh one in a sync (no catalog was saved yet).
    void migrateLegacyNames(const std::vector<BookMetadata>& catalog);
    // The home screen's books (up to 4). Their covers are looked up on the card
    // once until something changes (recentCover): that is a dozen file checks per
    // book, and the home screen asks on every key press.
    std::vector<RecentBookInfo> getRecentBooks();
    // Something shown on the home screen changed (positions, covers, the list).
    void touch() {
        _gen++;
        _countsDirty = true;   // a position may have made a book completed
    }

    // Catalog counters, kept up to date by scanLocalBooks / rebuildFilteredIndices
    // / setHideCompleted (the catalog screen redraws often).
    struct Counts {
        int total = 0, libraries = 0, authors = 0, series = 0, downloaded = 0, completed = 0, folders = 0;
    };
    Counts counts;
    // The counters, worked out again only when the list changed since (two sorts
    // of the whole library: not on every folder opened).
    const Counts& getCounts() {
        if (_countsDirty) refreshCounts();
        return counts;
    }
    bool hideCompleted = false;   // mirrors AppConfig::hideCompleted
    // A book counts as read from here on (the last pages are often notes or an
    // appendix); "hide completed" never hides the book being read.
    static constexpr float kCompletedAt = 0.97f;
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

    // resolveCoverPath(..., allowExtract) remembered in a small static table (no
    // heap: long-lived strings scattered by the home screen split the big free
    // block a book's index needs, and a book did not open right after a download).
    String recentCover(const String& filePath, const String& title, const String& coverPath);

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
    // Page turns are kept in memory (an SD write per page is slow and wears the
    // card); they are written while the reader sits idle once a few piled up or
    // a minute went by. Returns true when it wrote.
    bool persistIfDue();
    bool positionUnsaved() const { return _unsavedMoves > 0; }
    // Not a user move (closing, sleeping, font change, before a sync): refresh
    // page/xpath of lastBook and its record from the open reader, then save.
    void flushReaderPosition();
    // Copies a record's local position into lastBook (if it is the current book)
    // and into the matching library item.
    void applyRecordToLibrary(const BookProgressRecord& rec);

    // ---- formats of a book ----
    // Path of a format: the book's path with that format's extension.
    static String formatPath(const BookItem& b, const BookFormatSlot& f);
    // Makes formats[i] the active one (filePath, fileId, isDownloaded follow).
    static void selectFormat(BookItem& b, int i);
    // The format read last on this device (on the card, with a position), -1 if none.
    static int lastReadFormat(const BookItem& b);
    // Picks the active format: the one read last, else a readable one on the card,
    // else the best readable one on the server, else anything on the card.
    static void chooseFormat(BookItem& b);
    // Position of the active format from progress.json (after switching formats).
    void refreshProgressOf(BookItem& b);
    // Opens the list of the formats of the book at master index `idx`.
    void openFormats(int idx);
    // Leaves it (back to the list it was opened from).
    void closeFormats();

    // A title as a file name: the characters FAT does not take become "_".
    static String safeFileName(const String& title, const String& fallbackId);
    // Name (no folder, no extension) of a server book's files on the card: its
    // book and its cover. "<safe title> [<book id>]": two server books of one
    // title never share a file, a cover or a position.
    static String catalogStem(const String& title, int bookId);
    // Path of the preferred format of a catalog book, and of one of its formats
    // ("/books/<catalogStem>.epub").
    static String catalogBookPath(const BookMetadata& b);
    static String catalogFormatPath(const BookMetadata& b, const BookFormatEntry& f);
    // Server ids of a catalog entry: book id (from the OPDS id) and file id.
    static int catalogBookId(const BookMetadata& b);

private:
    LibraryManager() = default;
    bool _scanned = false;
    bool _countsDirty = true;
    uint32_t _gen = 1;
    int _unsavedMoves = 0;
    uint32_t _firstUnsavedAt = 0;
    void buildMaster();
    void refreshProgress();
    void refreshCounts();
    void rebuildRows();
    int internLibrary(const String& name);
};
