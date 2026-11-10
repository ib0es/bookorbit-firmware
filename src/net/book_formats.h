#pragma once

// Book file formats: one Book Orbit book can come in several (EPUB, FB2, MOBI,
// AZW3...), each a separate server file with its own fileId and progress.
// Pure C++ (no Arduino), host-tested in test/host/test_book_formats.cpp.

#include <cstdint>
#include <string>
#include <vector>

namespace BookFormats {

enum Kind : uint8_t { Unknown = 0, Epub, Fb2, Mobi, Azw3, Azw, Txt, KindCount };

// From an OPDS link: MIME type first, then its title ("EPUB", "FB2"...), then
// the extension in the address. Unknown if none of them says.
Kind fromMime(const std::string& mime, const std::string& title = "", const std::string& href = "");
// From a file name or path ("/books/x.fb2", "x.FB2.zip" is Unknown).
Kind fromPath(const std::string& path);

const char* extension(Kind k);   // ".epub", ".fb2"... "" for Unknown
const char* label(Kind k);       // "EPUB", "FB2"...
// The reader can open it now (the others are listed, but greyed out).
bool readable(Kind k);
// Lower is better: which format a book opens in when nothing was read yet.
int rank(Kind k);

// One format of a book as listed by the server.
struct Ref {
    int fileId = 0;
    Kind kind = Unknown;
    std::string url;   // empty: the canonical download address (see downloadPath)
};

// "/api/v1/opds/<bookId>/download?fileId=<fileId>": what Book Orbit lists for most
// formats. Such addresses are not stored (urlFor() rebuilds them), which saves
// a heap string per format and book.
std::string downloadPath(const std::string& bookId, int fileId);
std::string urlFor(const Ref& r, const std::string& bookId);
// Returns `url` or "" when it is the canonical address of that format (numeric
// book ids only).
std::string compactUrl(const std::string& url, const std::string& bookId, int fileId);

// Index of the format to open / download first: the best ranked readable one,
// else the best ranked one. -1 for an empty list.
int preferred(const std::vector<Ref>& refs);

// "EPUB · FB2 · AZW3" in rank order, each format once.
std::string labels(const std::vector<Kind>& kinds);

}  // namespace BookFormats
