// EngineTextCounter against FreeInkBook itself.  Run: test/host/run.sh
//
// 1. Unit cases: whitespace collapse, paragraphs, <br>, entities, skipped
//    elements, UTF-8, emitted() with a pending space.
// 2. Real books (paths on the command line): every chapter is laid out by the
//    engine; every id="" anchor it reports (chapter character offset) and its
//    character total must equal what the counter gives at that element.
#include "reader/engine/engine_text_counter.h"

#include <FreeInkBook.h>
#include <layout/ChapterLayout.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace freeink::book;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint32_t count(const std::string& xhtml) {
    EngineTextCounter c;
    c.feed(xhtml.data(), xhtml.size());
    return c.committed();
}

static std::string body(const std::string& inner) {
    return "<?xml version=\"1.0\"?><html><head><title>T x</title><style>p{a:b}</style></head><body>" + inner +
           "</body></html>";
}

static void testUnits() {
    printf("units\n");
    CHECK(count(body("<p>ab</p>")) == 2);
    CHECK(count(body("<p>  a \n\t b  </p>")) == 3);            // "a b"
    CHECK(count(body("<p>a</p>\n  <p> b</p>")) == 2);          // paragraphs add nothing
    CHECK(count(body("<p>a<br/>b</p>")) == 3);
    CHECK(count(body("<p>a <br/> b</p>")) == 4);               // "a\n b": the space before <br> is dropped, the one after counts
    CHECK(count(body("<p>&amp;&#x41;&nbsp;x</p>")) == 4);
    CHECK(count(body("<p>a <em>b</em> c</p>")) == 5);
    CHECK(count(body("<p>\xD0\x96\xD0\xB8\xD0\xB7\xD0\xBD\xD1\x8C</p>")) == 5);   // Cyrillic: code points
    CHECK(count(body("<div>a<p>b</p>c</div>")) == 3);
    CHECK(count(body("<p>a<!-- x y z -->b<script>var q = 1;</script>c</p>")) == 3);
    CHECK(count(body("<p title=\"a > b\">x</p>")) == 1);
    CHECK(count("<html><head><title>only head</title></head></html>") == 0);
    EngineTextCounter c;
    const std::string s = body("<p>ab ");
    c.feed(s.data(), s.find("</body>"));
    CHECK(c.committed() == 2 && c.emitted() == 3);              // the space waits for a character
}

// ------------------------------------------------------------- real books

class FileSource : public BookSource {
public:
    ~FileSource() override { if (f_) fclose(f_); }
    bool open(const char* p) {
        f_ = fopen(p, "rb");
        if (!f_) return false;
        fseek(f_, 0, SEEK_END);
        size_ = (uint64_t)ftell(f_);
        return true;
    }
    int32_t readAt(uint64_t off, void* dst, uint32_t len) override {
        if (fseek(f_, (long)off, SEEK_SET) != 0) return -1;
        return (int32_t)fread(dst, 1, len, f_);
    }
    uint64_t size() const override { return size_; }

private:
    FILE* f_ = nullptr;
    uint64_t size_ = 0;
};

class Metrics : public BookFont {
public:
    int16_t advance(uint32_t cp, uint16_t px, uint8_t) override { return (int16_t)(cp < 0x2E80 ? px / 2 : px); }
    int16_t lineHeight(uint16_t px) override { return (int16_t)(px * 5 / 4); }
    int16_t ascent(uint16_t px) override { return (int16_t)px; }
};

class AnchorSink : public PageSink {
public:
    std::map<uint32_t, uint32_t> anchors;   // id hash -> first char offset
    void onAnchor(uint32_t hash, uint32_t at) override { anchors.emplace(hash, at); }
    bool onPage(const Page&) override { return true; }
};

// The counter's offset at every id="" start tag: a counter fed byte by byte,
// read when a tag carrying id= has been fed.
static std::map<uint32_t, uint32_t> counterAnchors(const std::string& x, uint32_t& total) {
    std::map<uint32_t, uint32_t> out;
    EngineTextCounter c;
    size_t i = 0;
    while (i < x.size()) {
        if (x[i] != '<') { c.feed(&x[i], 1); i++; continue; }
        size_t end = i + 1;
        char quote = 0;
        while (end < x.size() && (quote || x[end] != '>')) {
            if (quote) { if (x[end] == quote) quote = 0; }
            else if (x[end] == '"' || x[end] == '\'') quote = x[end];
            end++;
        }
        const std::string tag = x.substr(i, end - i + 1);
        // The engine records the anchor before a block element flushes the paragraph.
        const uint32_t before = c.committed();
        c.feed(tag.data(), tag.size());
        if (tag.size() > 2 && tag[1] != '/' && tag[1] != '!' && tag[1] != '?') {
            const size_t at = tag.find(" id=");
            if (at != std::string::npos) {
                const char q = tag[at + 4];
                const size_t close = tag.find(q, at + 5);
                const std::string id = tag.substr(at + 5, close - at - 5);
                out.emplace(ZipCatalog::hashPath(id.c_str()), before);
            }
        }
        i = end + 1;
    }
    total = c.committed();
    return out;
}

static uint8_t g_book[1 << 20], g_scratch[1 << 20], g_parse[256 * 1024];

static void testBook(const char* path) {
    FileSource src;
    if (!src.open(path)) { printf("  skip %s (missing)\n", path); return; }
    Arena bookArena(g_book, sizeof(g_book)), scratch(g_scratch, sizeof(g_scratch));
    Book book;
    if (book.open(src, bookArena, scratch) != BookStatus::Ok) { CHECK(!"open"); return; }
    Metrics font;
    LayoutParams params;
    params.pageWidth = 480;
    params.pageHeight = 800;
    params.font = &font;
    uint32_t anchors = 0, chapters = 0, mismatched = 0;
    for (size_t s = 0; s < book.spineCount(); s++) {
        const ManifestItem* item = book.spineItem(s);
        const ZipEntry* entry = book.zip().find(item->href);
        if (!entry) continue;
        // The chapter's bytes, inflated.
        std::string x;
        {
            const size_t m = scratch.mark();
            ZipEntryReader r;
            if (r.open(src, *entry, scratch) != BookStatus::Ok) { CHECK(!"inflate"); continue; }
            char buf[4096];
            for (int32_t n; (n = r.read(buf, sizeof(buf))) > 0;) x.append(buf, (size_t)n);
            scratch.release(m);
        }
        AnchorSink sink;
        uint32_t engineTotal = 0;
        Arena parse(g_parse, sizeof(g_parse));
        const size_t m = scratch.mark();
        const BookStatus st = ChapterLayout::layout(src, book.zip(), *entry, item->href, params, scratch, sink,
                                                    nullptr, &engineTotal, &parse);
        scratch.release(m);
        if (st != BookStatus::Ok) { CHECK(!"layout"); continue; }
        uint32_t total = 0;
        const auto mine = counterAnchors(x, total);
        chapters++;
        if (total != engineTotal) {
            mismatched++;
            printf("  %s spine %zu: %u characters, the engine %u\n", path, s, total, engineTotal);
        }
        for (const auto& a : sink.anchors) {
            auto it = mine.find(a.first);
            if (it == mine.end()) continue;   // id inside a skipped element
            anchors++;
            if (it->second != a.second) {
                mismatched++;
                if (mismatched < 8) printf("  %s spine %zu: anchor at %u, the engine %u\n", path, s, it->second, a.second);
            }
        }
    }
    printf("  %s: %u chapters, %u anchors, %u differ\n", path, chapters, anchors, mismatched);
    CHECK(mismatched == 0);
}

int main(int argc, char** argv) {
    testUnits();
    for (int i = 1; i < argc; i++) testBook(argv[i]);
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
