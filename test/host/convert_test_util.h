#pragma once
// Shared by the converter tests: in-memory files, and an EPUB opened and laid
// out by the engine.
#include "convert/convert_io.h"

#include <FreeInkBook.h>
#include <epub/ZipCatalog.h>
#include <layout/ChapterLayout.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace freeink::book;

namespace Platform {
__attribute__((weak)) void feedWatchdog() {}
}  // namespace Platform

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        g_checks++;                                               \
        if (!(c)) {                                               \
            g_failed++;                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                         \
    } while (0)

// ------------------------------------------------------------------ files

class MemIn : public convert::InFile {
public:
    explicit MemIn(std::string d) : _d(std::move(d)) {}
    bool rewind() override {
        _pos = 0;
        return true;
    }
    bool seek(uint64_t pos) override {
        if (pos > _d.size()) return false;
        _pos = (size_t)pos;
        return true;
    }
    int32_t read(void* dst, uint32_t len) override {
        const size_t n = std::min<size_t>(len, _d.size() - _pos);
        memcpy(dst, _d.data() + _pos, n);
        _pos += n;
        return (int32_t)n;
    }
    uint64_t size() const override { return _d.size(); }

private:
    std::string _d;
    size_t _pos = 0;
};

class MemOut : public convert::OutFile {
public:
    std::string d;
    bool write(const void* p, uint32_t n) override {
        if (_pos + n > d.size()) d.resize(_pos + n);
        memcpy(&d[_pos], p, n);
        _pos += n;
        return true;
    }
    bool seek(uint32_t pos) override {
        _pos = pos;
        return pos <= d.size();
    }
    uint32_t tell() override { return (uint32_t)_pos; }

private:
    size_t _pos = 0;
};

class MemTemp : public convert::TempFile {
public:
    bool beginWrite() override {
        _d.clear();
        return true;
    }
    bool write(const void* p, uint32_t n) override {
        _d.append((const char*)p, n);
        return true;
    }
    bool beginRead() override {
        _pos = 0;
        return true;
    }
    int32_t read(void* dst, uint32_t len) override {
        const size_t n = std::min<size_t>(len, _d.size() - _pos);
        memcpy(dst, _d.data() + _pos, n);
        _pos += n;
        return (int32_t)n;
    }

private:
    std::string _d;
    size_t _pos = 0;
};

class MemSource : public BookSource {
public:
    explicit MemSource(const std::string& d) : _d(d) {}
    int32_t readAt(uint64_t off, void* dst, uint32_t len) override {
        if (off >= _d.size()) return 0;
        const size_t n = std::min<size_t>(len, _d.size() - off);
        memcpy(dst, _d.data() + off, n);
        return (int32_t)n;
    }
    uint64_t size() const override { return _d.size(); }

private:
    const std::string& _d;
};

class Metrics : public BookFont {
public:
    int16_t advance(uint32_t cp, uint16_t px, uint8_t) override { return (int16_t)(cp < 0x2E80 ? px / 2 : px); }
    int16_t lineHeight(uint16_t px) override { return (int16_t)(px * 5 / 4); }
    int16_t ascent(uint16_t px) override { return (int16_t)px; }
};

class CountSink : public PageSink {
public:
    uint32_t pages = 0, images = 0;
    bool onPage(const Page& p) override {
        pages++;
        images += p.imageCount;
        return true;
    }
};

static std::string readFile(const std::string& path) {
    std::string d;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return d;
    char buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) d.append(buf, n);
    fclose(f);
    return d;
}

static uint8_t g_book[2 << 20], g_scratch[1 << 20], g_parse[256 * 1024];

struct Opened {
    bool ok = false;
    std::vector<std::string> chapters;   // xhtml text per spine item
    std::vector<std::string> toc;        // "depth|title|href#fragment"
    uint32_t pages = 0, images = 0;
    std::string title, author, lang;
    bool cover = false;
};

// Opens the EPUB with the engine, lays out every chapter, reads chapter texts.
static Opened openEpub(const std::string& epub) {
    Opened o;
    MemSource src(epub);
    Arena bookArena(g_book, sizeof(g_book)), scratch(g_scratch, sizeof(g_scratch));
    Book book;
    if (book.open(src, bookArena, scratch) != BookStatus::Ok) return o;
    o.title = book.metadata().title;
    o.author = book.metadata().author;
    o.lang = book.metadata().language;
    for (size_t i = 0; i < book.manifestCount(); i++)
        if (book.manifestItem(i)->isCoverImage) o.cover = true;
    for (size_t i = 0; i < book.tocCount(); i++) {
        const TocEntry* t = book.tocEntry(i);
        o.toc.push_back(std::to_string(t->depth) + "|" + t->title + "|" + t->href + "#" +
                        (t->fragment ? t->fragment : ""));
    }
    Metrics font;
    LayoutParams params;
    params.pageWidth = 480;
    params.pageHeight = 800;
    params.font = &font;
    o.ok = true;
    for (size_t s = 0; s < book.spineCount(); s++) {
        const ManifestItem* item = book.spineItem(s);
        const ZipEntry* entry = book.zip().find(item->href);
        if (!entry) {
            o.ok = false;
            continue;
        }
        std::string x;
        {
            const size_t m = scratch.mark();
            ZipEntryReader r;
            if (r.open(src, *entry, scratch) != BookStatus::Ok) {
                o.ok = false;
                continue;
            }
            char buf[4096];
            for (int32_t n; (n = r.read(buf, sizeof(buf))) > 0;) x.append(buf, (size_t)n);
            scratch.release(m);
        }
        o.chapters.push_back(x);
        CountSink sink;
        Arena parse(g_parse, sizeof(g_parse));
        const size_t m = scratch.mark();
        const BookStatus st = ChapterLayout::layout(src, book.zip(), *entry, item->href, params, scratch, sink, nullptr,
                                                    nullptr, &parse);
        scratch.release(m);
        if (st != BookStatus::Ok) {
            printf("  chapter %zu (%s): %s\n", s, item->href, bookStatusName(st));
            o.ok = false;
        }
        o.pages += sink.pages;
        o.images += sink.images;
    }
    return o;
}

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

static int chapterWith(const Opened& o, const std::string& s) {
    for (size_t i = 0; i < o.chapters.size(); i++)
        if (o.chapters[i].find(s) != std::string::npos) return (int)i;
    return -1;
}

// Files in `dir` ending in `ext`.
static std::vector<std::string> filesIn(const char* dir, const char* ext) {
    std::vector<std::string> out;
    DIR* d = opendir(dir);
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        const size_t k = strlen(ext);
        if (n.size() > k && n.compare(n.size() - k, k, ext) == 0) out.push_back(std::string(dir) + "/" + n);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}
