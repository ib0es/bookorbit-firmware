#pragma once

// Streaming OPDS 1.x (Atom) catalog parser for Book Orbit feeds.
//
// The feed is pushed in arbitrary chunks via feed(); only the current <entry>
// (bounded by maxEntryBytes) plus one partial tag is kept in memory, so peak
// RAM no longer grows with the size of the catalog. Each complete book entry is
// handed to the callback as soon as its </entry> arrives.
//
// Pure C++ (no Arduino dependencies) so it can be unit-tested on the host:
// see test/host/test_opds_stream_parser.cpp.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "book_formats.h"

struct OpdsBookEntry {
    std::string id;          // "urn:bookorbit:book:123" -> "123"
    std::string title;
    std::string author;
    std::string downloadUrl;
    std::string coverUrl;
    std::string series;      // from rel="http://opds-spec.org/sort/series"
    int seriesIndex = 0;     // parsed from series title e.g. "Series #1"
    int fileId = 0;          // from "fileId=" in downloadUrl, else numeric id
    // Every acquisition link (one per format). downloadUrl / fileId above are
    // the preferred one (BookFormats::preferred). A canonical download address
    // is stored empty (BookFormats::compactUrl).
    std::vector<BookFormats::Ref> formats;
};

struct OpdsNavEntry {
    std::string id;
    std::string title;
    std::string href;
    std::string content;
};

class OpdsStreamParser {
public:
    // Return false from the callback to stop parsing (e.g. book limit reached).
    using EntryCallback = std::function<bool(const OpdsBookEntry&)>;

    static constexpr size_t DEFAULT_MAX_ENTRY_BYTES = 16 * 1024;

    explicit OpdsStreamParser(EntryCallback onEntry, size_t maxEntryBytes = DEFAULT_MAX_ENTRY_BYTES);

    // Feed the next chunk of the response body. Returns false once parsing has
    // been stopped by the callback.
    bool feed(const char* data, size_t len);

    bool stopped() const { return _stopped; }
    size_t booksParsed() const { return _books; }
    size_t entriesSkipped() const { return _skipped; }   // oversized, or without title/download link
    // Feed-level <link rel="next" href="..."> (pagination), entity-decoded; empty if none.
    const std::string& nextPageUrl() const { return _nextUrl; }

    // Exposed for tests.
    static std::string decodeEntities(const std::string& in);
    static bool parseEntry(const std::string& entryXml, OpdsBookEntry& out);
    static bool parseNavEntry(const std::string& entryXml, OpdsNavEntry& out);
    static std::vector<OpdsNavEntry> parseNavigationFeed(const std::string& xml);

private:
    void scanFeedLevel(size_t end);   // look for rel="next" in _buf[0, end)

    EntryCallback _onEntry;
    size_t _maxEntryBytes;
    std::string _buf;
    bool _inEntry = false;
    bool _discardingEntry = false;    // current entry exceeded the limit; skip to </entry>
    bool _stopped = false;
    size_t _books = 0;
    size_t _skipped = 0;
    std::string _nextUrl;
};
