// Host unit tests for OpdsStreamParser.  Run: test/host/run.sh
#include "net/opds_stream_parser.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; if (!((a) == (b))) { g_failed++; \
    printf("  FAIL %s:%d: %s == %s  (got '%s')\n", __FILE__, __LINE__, #a, #b, std::string(a).c_str()); } } while (0)

static const char* FEED =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
    "  <id>urn:bookorbit:catalog</id><title>Book Orbit</title>\n"
    "  <link rel=\"self\" href=\"/api/v1/opds/catalog\"/>\n"
    "  <link rel=\"next\" href=\"/api/v1/opds/catalog?page=2&amp;size=50\"/>\n"
    "  <entry>\n"
    "    <title>Мастер и Маргарита</title>\n"
    "    <id>urn:bookorbit:book:17</id>\n"
    "    <author><name>Михаил Булгаков</name></author>\n"
    "    <link rel=\"http://opds-spec.org/image/thumbnail\" href=\"/covers/17?w=200&amp;h=300\" type=\"image/jpeg\"/>\n"
    "    <link rel=\"http://opds-spec.org/acquisition\" href=\"/api/v1/opds/download?fileId=42&amp;x=1\" type=\"application/epub+zip\"/>\n"
    "  </entry>\n"
    "  <entry>\n"
    "    <title type=\"text\">Tom &amp; Jerry &#x2014; &#1040;&lt;b&gt;</title>\n"
    "    <id>urn:bookorbit:book:99</id>\n"
    "    <link rel='http://opds-spec.org/acquisition' href='/dl/99.epub'/>\n"
    "  </entry>\n"
    "  <entry>\n"
    "    <title>Navigation only, no download</title>\n"
    "    <id>urn:x:nav</id>\n"
    "    <link rel=\"subsection\" href=\"/nav\"/>\n"
    "  </entry>\n"
    "  <entry><title><![CDATA[Raw <title> & stuff]]></title><identifier>bad</identifier><id>urn:a:5</id>"
    "<link type=\"image/png\" href=\"/img5.png\"/><link rel=\"acquisition\" href=\"/d5\"/></entry>\n"
    "</feed>\n";

static std::vector<OpdsBookEntry> parseInChunks(const std::string& xml, size_t chunk, std::string* next = nullptr,
                                                size_t* skipped = nullptr, size_t maxEntry = OpdsStreamParser::DEFAULT_MAX_ENTRY_BYTES) {
    std::vector<OpdsBookEntry> out;
    OpdsStreamParser p([&](const OpdsBookEntry& e) { out.push_back(e); return true; }, maxEntry);
    for (size_t i = 0; i < xml.size(); i += chunk) {
        p.feed(xml.data() + i, std::min(chunk, xml.size() - i));
    }
    if (next) *next = p.nextPageUrl();
    if (skipped) *skipped = p.entriesSkipped();
    return out;
}

static void testBasicFeed() {
    printf("testBasicFeed\n");
    std::string next; size_t skipped = 0;
    auto books = parseInChunks(FEED, 1 << 20, &next, &skipped);
    CHECK(books.size() == 3);
    CHECK(skipped == 1);
    CHECK_EQ(next, "/api/v1/opds/catalog?page=2&size=50");
    if (books.size() < 3) return;

    CHECK_EQ(books[0].title, "Мастер и Маргарита");
    CHECK_EQ(books[0].author, "Михаил Булгаков");
    CHECK_EQ(books[0].id, "17");
    CHECK(books[0].fileId == 42);
    CHECK_EQ(books[0].downloadUrl, "/api/v1/opds/download?fileId=42&x=1");
    CHECK_EQ(books[0].coverUrl, "/covers/17?w=200&h=300");

    CHECK_EQ(books[1].title, "Tom & Jerry \xE2\x80\x94 \xD0\x90<b>");
    CHECK(books[1].fileId == 99);            // no fileId= -> numeric id
    CHECK_EQ(books[1].downloadUrl, "/dl/99.epub");
    CHECK_EQ(books[1].author, "");

    CHECK_EQ(books[2].title, "Raw <title> & stuff");
    CHECK_EQ(books[2].id, "5");              // <identifier> must not match <id>
    CHECK_EQ(books[2].coverUrl, "/img5.png");
}

static void testEveryChunkSize() {
    printf("testEveryChunkSize\n");
    auto ref = parseInChunks(FEED, 1 << 20);
    bool allSame = true;
    for (size_t chunk = 1; chunk <= 64; chunk++) {
        std::string next;
        auto got = parseInChunks(FEED, chunk, &next);
        bool same = got.size() == ref.size() && next == "/api/v1/opds/catalog?page=2&size=50";
        for (size_t i = 0; same && i < got.size(); i++) {
            same = got[i].title == ref[i].title && got[i].downloadUrl == ref[i].downloadUrl &&
                   got[i].coverUrl == ref[i].coverUrl && got[i].fileId == ref[i].fileId;
        }
        if (!same) { allSame = false; printf("  mismatch at chunk=%zu\n", chunk); }
    }
    CHECK(allSame);
}

static void testOversizedEntrySkipped() {
    printf("testOversizedEntrySkipped\n");
    std::string xml = "<feed><entry><title>Big</title><summary>";
    xml += std::string(5000, 'x');
    xml += "</summary><link rel=\"acquisition\" href=\"/big\"/></entry>"
           "<entry><title>Small</title><link rel=\"acquisition\" href=\"/small\"/></entry></feed>";
    size_t skipped = 0;
    auto books = parseInChunks(xml, 100, nullptr, &skipped, 1024);
    CHECK(books.size() == 1);
    CHECK(skipped == 1);
    if (!books.empty()) CHECK_EQ(books[0].title, "Small");
}

static void testStopFromCallback() {
    printf("testStopFromCallback\n");
    int n = 0;
    OpdsStreamParser p([&](const OpdsBookEntry&) { return ++n < 2; });
    std::string xml = FEED;
    bool cont = p.feed(xml.data(), xml.size());
    CHECK(!cont);
    CHECK(p.stopped());
    CHECK(n == 2);
    CHECK(!p.feed("<entry>", 7));
}

static void testBufferStaysBounded() {
    printf("testBufferStaysBounded\n");
    // 2000 entries streamed in 512-byte chunks must all be parsed.
    std::string entry = "<entry><title>Book</title><id>urn:b:1</id>"
                        "<link rel=\"http://opds-spec.org/acquisition\" href=\"/d?fileId=1\"/></entry>\n";
    std::string xml = "<feed>";
    for (int i = 0; i < 2000; i++) xml += entry;
    xml += "</feed>";
    auto books = parseInChunks(xml, 512);
    CHECK(books.size() == 2000);
}

static void testEntitiesAndAttributes() {
    printf("testEntitiesAndAttributes\n");
    using P = OpdsStreamParser;
    CHECK(P::decodeEntities("plain") == "plain");
    CHECK(P::decodeEntities("a &amp; b &lt;c&gt; &quot;d&quot; &apos;e&apos;") == "a & b <c> \"d\" 'e'");
    CHECK(P::decodeEntities("&#65;&#x42;&#X43;") == "ABC");
    CHECK(P::decodeEntities("&#1092;") == "\xD1\x84");                    // ф, 2 bytes
    CHECK(P::decodeEntities("&#x20AC;") == "\xE2\x82\xAC");              // €, 3 bytes
    CHECK(P::decodeEntities("&#x1F4D6;") == "\xF0\x9F\x93\x96");        // 📖, 4 bytes
    CHECK(P::decodeEntities("&#x110000;") == "");                          // beyond Unicode: dropped
    CHECK(P::decodeEntities("&#xZZ; &#0; &nbsp; & alone &verylongentityname;") ==
          "&#xZZ; &#0; &nbsp; & alone &verylongentityname;");             // unknown: kept as is
    CHECK(P::decodeEntities("&amp") == "&amp");                            // no ';'

    // Attributes: single quotes, spaces around '=', a name inside another name, unterminated value.
    OpdsBookEntry e;
    CHECK(P::parseEntry("<entry><title>T</title><id>urn:x:7</id>"
                        "<link xhref='bad' rel = 'http://opds-spec.org/acquisition' href = '/d?a=1&amp;fileId=5'/></entry>", e));
    CHECK(e.downloadUrl == "/d?a=1&fileId=5" && e.fileId == 5 && e.id == "7");
    CHECK(!P::parseEntry("<entry><title>T</title><link rel=\"http://opds-spec.org/acquisition\" href=\"/d/></entry>", e));
    CHECK(!P::parseEntry("<entry><title>T</title><link rel=\"http://opds-spec.org/acquisition\" href=x/></entry>", e));
    CHECK(!P::parseEntry("<entry><title/><link rel=\"http://opds-spec.org/acquisition\" href=\"/d\"/></entry>", e));
    CHECK(!P::parseEntry("<entry><title>T</title><link rel=\"x\" href=\"/d\"", e));   // tag never closed
    CHECK(P::parseEntry("<entry><title><![CDATA[ A & B ]]></title><id>9</id>"
                        "<link type=\"application/epub+zip\" href=\"/get/9\"/></entry>", e));
    CHECK(e.title == "A & B" && e.fileId == 9);
}

static void testSeriesParsing() {
    printf("testSeriesParsing\n");
    const std::string entryWithSeries =
        "<entry>\n"
        "  <title>El ojo del mundo</title>\n"
        "  <id>urn:bookorbit:book:2</id>\n"
        "  <author><name>Robert Jordan</name></author>\n"
        "  <link rel=\"http://opds-spec.org/sort/series\" href=\"/api/v1/opds/catalog?seriesId=1\" title=\"The Wheel of Time #1\"/>\n"
        "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/dl/2.epub\"/>\n"
        "</entry>";
    OpdsBookEntry out;
    CHECK(OpdsStreamParser::parseEntry(entryWithSeries, out));
    CHECK_EQ(out.series, "The Wheel of Time");
    CHECK(out.seriesIndex == 1);

    const std::string entryNoIndex =
        "<entry>\n"
        "  <title>Standalone Series Book</title>\n"
        "  <id>urn:bookorbit:book:3</id>\n"
        "  <link rel=\"http://opds-spec.org/sort/series\" href=\"/api/v1/opds/catalog?seriesId=2\" title=\"Foundation\"/>\n"
        "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/dl/3.epub\"/>\n"
        "</entry>";
    OpdsBookEntry out2;
    CHECK(OpdsStreamParser::parseEntry(entryNoIndex, out2));
    CHECK_EQ(out2.series, "Foundation");
    CHECK(out2.seriesIndex == 0);

    // Book Orbit lists the full cover first, then the thumbnail: the thumbnail wins
    // (the full one is a progressive print-size JPEG the device cannot decode).
    const std::string entryCovers =
        "<entry><title>Covers</title><id>urn:bookorbit:book:3</id>\n"
        "  <link rel=\"http://opds-spec.org/image\" href=\"/api/v1/opds/3/cover?t=1\" type=\"image/jpeg\"/>\n"
        "  <link rel=\"http://opds-spec.org/image/thumbnail\" href=\"/api/v1/opds/3/thumbnail?t=1\" type=\"image/jpeg\"/>\n"
        "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/api/v1/opds/3/download?fileId=3\"/></entry>";
    OpdsBookEntry outC;
    CHECK(OpdsStreamParser::parseEntry(entryCovers, outC));
    CHECK_EQ(outC.coverUrl, "/api/v1/opds/3/thumbnail?t=1");

    // A related link whose address mentions a series is not the book's series.
    const std::string entryRelated =
        "<entry>\n"
        "  <title>Other Book</title>\n"
        "  <id>urn:bookorbit:book:4</id>\n"
        "  <link rel=\"related\" href=\"/api/v1/opds/series/7\" title=\"More in this series\"/>\n"
        "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/dl/4.epub\"/>\n"
        "</entry>";
    OpdsBookEntry out3;
    CHECK(OpdsStreamParser::parseEntry(entryRelated, out3));
    CHECK(out3.series.empty());
}

static void testLibrariesFeedParsing() {
    printf("testLibrariesFeedParsing\n");
    const std::string libsFeed =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
        "  <title>Libraries</title>\n"
        "  <id>urn:bookorbit:libraries</id>\n"
        "  <entry>\n"
        "    <title>Фентези</title>\n"
        "    <id>urn:bookorbit:library:1</id>\n"
        "    <content type=\"text\">20 books</content>\n"
        "    <link rel=\"subsection\" href=\"/api/v1/opds/catalog?libraryId=1\" type=\"application/atom+xml;profile=opds-catalog\"/>\n"
        "  </entry>\n"
        "  <entry>\n"
        "    <title>Фантастика</title>\n"
        "    <id>urn:bookorbit:library:2</id>\n"
        "    <content type=\"text\">7 books</content>\n"
        "    <link rel=\"subsection\" href=\"/api/v1/opds/catalog?libraryId=2\" type=\"application/atom+xml;profile=opds-catalog\"/>\n"
        "  </entry>\n"
        "</feed>\n";

    auto navs = OpdsStreamParser::parseNavigationFeed(libsFeed);
    CHECK(navs.size() == 2);
    if (navs.size() == 2) {
        CHECK_EQ(navs[0].title, "Фентези");
        CHECK_EQ(navs[0].id, "urn:bookorbit:library:1");
        CHECK_EQ(navs[0].content, "20 books");
        CHECK_EQ(navs[0].href, "/api/v1/opds/catalog?libraryId=1");

        CHECK_EQ(navs[1].title, "Фантастика");
        CHECK_EQ(navs[1].id, "urn:bookorbit:library:2");
        CHECK_EQ(navs[1].content, "7 books");
        CHECK_EQ(navs[1].href, "/api/v1/opds/catalog?libraryId=2");
    }
}

int main() {
    testBasicFeed();
    testSeriesParsing();
    testLibrariesFeedParsing();
    testEveryChunkSize();
    testOversizedEntrySkipped();
    testStopFromCallback();
    testBufferStaysBounded();
    testEntitiesAndAttributes();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
