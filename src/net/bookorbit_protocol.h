#pragma once

// Book Orbit wire format: building request bodies and parsing responses.
// Pure (std::string + ArduinoJson, no HTTP/Wi-Fi), host-tested in
// test/host/test_bookorbit_protocol.cpp. BookOrbitClient does the transport.

#include <cstdint>
#include <string>
#include <vector>

namespace BookOrbitProtocol {

struct Progress {
    bool exists = false;       // the server has a progress record for the book
    float percent = 0.0f;      // 0..1
    int page = 0;
    std::string cfi;           // EPUB CFI of the web reader ("cfi"), may be empty
    uint32_t updatedAt = 0;    // UTC seconds, 0 = unknown
};

// POST /api/v1/auth/login response -> access token. False if none.
bool parseLoginToken(const std::string& json, std::string& token);

// GET /api/v1/books/:id/progress response (array or {"items":[...]}): one record
// per file (format) of the book. With fileId > 0 only that file's record counts
// (formats are read separately: EPUB at 40% says nothing about the FB2); the most
// recently updated record wins. Returns false only for malformed JSON;
// out.exists=false when there is no record for the file.
bool parseBookProgress(const std::string& json, Progress& out, int fileId = 0);

// PUT /api/v1/reading-progress body. percent is 0..1 (values above 1 are taken as 0..100).
// The server REPLACES the whole position with each write: a field left out is
// cleared (no cfi -> the web reader loses its place). With a cfi the page is not
// sent: the web reader numbers pages differently.
std::string buildProgressWrite(int fileId, float percent, int page, const std::string& cfi = "");

// PUT /api/v1/reading-progress response -> server updatedAt (0 if absent).
uint32_t parseProgressWriteUpdatedAt(const std::string& json);

// A bookmark of the web reader (/api/v1/books/:id/bookmarks).
struct Bookmark {
    int id = 0;
    std::string cfi;     // EPUB CFI (of the file asked for with ?readerFileId)
    std::string title;
};

// GET /api/v1/books/:id/bookmarks response (array or {"items":[...]}): the text
// bookmarks; audiobook ones (positionSeconds) and ones without a CFI in this
// file are left out. False only for malformed JSON.
bool parseBookmarks(const std::string& json, std::vector<Bookmark>& out);

// POST /api/v1/books/:id/bookmarks body. The title is required by the server
// (1..500 characters): an empty one becomes "Bookmark", a long one is cut at a
// character boundary.
std::string buildBookmarkCreate(int fileId, const std::string& cfi, const std::string& title);

// POST response -> the bookmark's id (0 if none).
int parseCreatedBookmarkId(const std::string& json);

}  // namespace BookOrbitProtocol
