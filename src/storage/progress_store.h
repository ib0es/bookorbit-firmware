#pragma once

// Per-book reading progress and sync state, persisted in /books/progress.json.
//
// This is the single source of truth for "where am I in book X on this
// device". The OPDS catalog cache (/books/catalog.json) only mirrors what the
// server reported and is used to show progress of books never opened here.

#include <Arduino.h>
#include <deque>
#include "../core/progress_sync.h"

struct BookProgressRecord {
    int fileId = 0;          // Book Orbit file id (used for uploads), 0 = unknown
    int bookId = 0;          // Book Orbit book id (used for downloads of progress), 0 = unknown
    String filePath;         // local path on the SD card
    String title;
    ProgressSync::BookState state;
};

class ProgressStore {
public:
    static ProgressStore& instance() {
        static ProgressStore s;
        return s;
    }

    static constexpr const char* kPath = "/books/progress.json";

    bool load();
    bool save();             // writes only if something changed since the last save

    // Lookup by server file id first, then by exact local path. nullptr if none.
    BookProgressRecord* find(int fileId, const String& filePath);

    // Finds or creates the record; fills in ids/path/title that were missing.
    BookProgressRecord& getOrCreate(int fileId, int bookId, const String& filePath, const String& title);

    // Drops the record of a local path (any letter case); true if there was one.
    bool remove(const String& filePath);

    // A deque: references to records stay valid when other records are added.
    std::deque<BookProgressRecord>& records() { return _records; }

    // Mark the store modified after changing a record in place.
    void touch() { _modified = true; }

    // For tests / simulator resets.
    void clear() {
        _records.clear();
        _modified = true;
    }

private:
    ProgressStore() = default;
    std::deque<BookProgressRecord> _records;
    bool _modified = false;
    bool _readOnly = false;   // the file could not be read for lack of memory: not overwritten
};
