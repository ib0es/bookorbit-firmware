#pragma once

// What the firmware keeps about books on the card: the catalog entries
// (/books/catalog.json) and the book being read (/books/last_book.json).
// Plain data, shared by the network client, the library and the stores.

#include <Arduino.h>
#include <vector>

// One format (file) of a catalog book. `url` is empty for the canonical download
// address "/api/v1/opds/<bookId>/download?fileId=<fileId>" (most formats), so a
// catalog of a few hundred books does not hold an address per format.
struct BookFormatEntry {
    int fileId = 0;
    uint8_t kind = 0;   // BookFormats::Kind
    String url;
};

struct BookMetadata {
    String id;
    int fileId = 0;     // the preferred format (BookFormats::preferred): first download, progress snapshot
    String title;
    String author;
    String series;
    int seriesIndex = 0;
    std::vector<BookFormatEntry> formats;   // every format, the preferred one included
    String coverUrl;
    // No reading position: a book's position is asked for once it is on the card
    // (download, sync); one only on the server shows none.
    String library;                 // Server library name e.g. "Фентези", "Фантастика"

    // Download address of a format of this book (rebuilt when canonical).
    String urlOf(const BookFormatEntry& f) const {
        if (f.url.length() > 0) return f.url;
        if (id.length() == 0 || f.fileId <= 0) return String();
        return String("/api/v1/opds/") + id + "/download?fileId=" + String(f.fileId);
    }
    const BookFormatEntry* preferredFormat() const {
        for (const auto& f : formats) if (f.fileId == fileId) return &f;
        return formats.empty() ? nullptr : &formats[0];
    }
    String downloadUrl() const {
        const BookFormatEntry* f = preferredFormat();
        return f ? urlOf(*f) : String();
    }
};

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
