#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include "bookorbit_protocol.h"

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
    float lastProgress = 0.0f; // 0.0 to 1.0
    int lastPage = 0;
    String xpath;
    uint32_t serverUpdatedAt = 0;   // server snapshot only; local state is in ProgressStore
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

struct ServerLibrary {
    int id = 0;
    String name;
    String href;
    int bookCount = 0;
};

// Reading position as stored by Book Orbit (/api/v1/books/:id/progress).
struct ServerProgress {
    bool exists = false;      // the server has a progress record for this book
    float percent = 0.0f;     // 0.0 .. 1.0
    int page = 0;
    String cfi;               // EPUB CFI of the web reader, empty if none
    uint32_t updatedAt = 0;   // UNIX seconds, 0 if unknown
};

// Diagnostics of the last fetchCatalog() call (shown on screen when it yields 0 books).
struct CatalogFetchInfo {
    int httpCode = 0;
    int streamError = 0;       // HTTPClient::writeToStream() result when negative
    uint32_t bytes = 0;        // body bytes received (all pages)
    uint32_t skipped = 0;      // <entry> elements without title/acquisition link
    uint32_t heapStart = 0;    // free heap before the first page
    uint32_t heapMin = 0;      // lowest free heap seen while parsing
    bool truncated = false;    // stopped by OPDS_MAX_BOOKS / OPDS_MIN_FREE_HEAP
    // True when an empty result is trustworthy (the server really has no books).
    bool cleanEmpty() const { return httpCode == 200 && streamError == 0 && bytes > 0 && skipped == 0 && !truncated; }
};

class BookOrbitClient {
public:
    BookOrbitClient();

    CatalogFetchInfo lastCatalog;

    // Wi-Fi Management
    bool connectWiFi(const char* ssid, const char* pass, uint32_t timeoutMs = 15000, bool (*checkCancel)() = nullptr);
    // Starts connecting and returns at once; poll isConnected(). For background jobs.
    void beginWiFi(const char* ssid, const char* pass);
    // Called at the start of every blocking connectWiFi(): a foreground network
    // operation takes the radio over from a background job.
    void setForegroundConnectHook(void (*hook)()) { _foregroundHook = hook; }
    void disconnectWiFi();
    bool isConnected();
    int getRssi();

    // Server Health & Status
    bool pingServer(const String& serverHost);

    // Authentication (Login with username/password to get session JWT token)
    bool login(const String& serverHost, const String& username, const String& password, String& outToken);

    // OPDS Catalog & Library
    bool fetchLibraries(const String& serverHost, const String& username, const String& password, const String& token, std::vector<ServerLibrary>& outLibraries);
    bool fetchCatalogFromUrl(const String& serverHost, const String& initialUrl, const String& username, const String& password, const String& token, const String& libraryName, std::vector<BookMetadata>& outBooks);
    bool fetchCatalog(const String& serverHost, const String& username, const String& password, const String& token, std::vector<BookMetadata>& outBooks);

    // File / Book Download (Streams directly to MicroSD)
    bool downloadFile(const String& serverHost, const String& fileUrl, const String& destSdPath, const String& username, const String& password, const String& token = "", void (*onProgress)(int percent) = nullptr, bool (*checkCancel)() = nullptr);
    bool downloadBook(const String& serverHost, const String& downloadUrl, const String& destSdPath, const String& username, const String& password, const String& token = "", void (*onProgress)(int percent) = nullptr, bool (*checkCancel)() = nullptr);
    bool downloadCover(const String& serverHost, const String& coverUrl, const String& destSdPath, const String& username, const String& password, const String& token = "", bool (*checkCancel)() = nullptr);

    // Native Book Orbit Reading Progress Sync.
    // `token` is refreshed in place (login with username/password) when missing or rejected.
    // fetchBookProgress returns false on a network/HTTP error; true with out.exists=false
    // when the server simply has no progress for the book.
    // Only the record of `fileId` (the format read here) counts; 0 = any file of the book.
    bool fetchBookProgress(const String& serverHost, const String& username, const String& password, String& token, int bookId, int fileId, ServerProgress& out);
    bool updateBookProgress(const String& serverHost, const String& username, const String& password, String& token, int fileId, float percent, int page, const String& cfi = "", uint32_t* outUpdatedAt = nullptr);

    // Bookmarks of the web reader. fetchBookmarks: CFIs in file `fileId`'s terms
    // (the server converts those made in another EPUB of the book); false on a
    // network/HTTP error. createBookmark: the new (or the server's existing,
    // same-place) bookmark's id, 0 on failure. deleteBookmark: true when it is
    // gone (also if it was already).
    bool fetchBookmarks(const String& serverHost, const String& username, const String& password, String& token,
                        int bookId, int fileId, std::vector<BookOrbitProtocol::Bookmark>& out);
    int createBookmark(const String& serverHost, const String& username, const String& password, String& token,
                       int bookId, int fileId, const String& cfi, const String& title);
    bool deleteBookmark(const String& serverHost, const String& username, const String& password, String& token,
                        int bookId, int bookmarkId);

private:
    // A request with the session token: logs in first when there is none, and
    // once more when the server answers 401. Returns the HTTP code (<0 on a
    // network error); the body of a 2xx answer goes to `response`.
    int authedRequest(const String& serverHost, const String& username, const String& password, String& token,
                      const char* method, const String& path, const String& body, String* response);
    void (*_foregroundHook)() = nullptr;
    String buildUrl(const String& host, const String& path);
    static String urlEncode(const String& str);
};

extern BookOrbitClient BookOrbit;
