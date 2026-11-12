#pragma once

// What the parts of BookReader share (A8: book_reader.cpp was split into
// open/index and the spine table, layout with the background job, cover
// extraction, positions/CFI/contents, page rendering, and the preview).
// Internal to src/reader/book_reader*.cpp.

#include "book_reader.h"
#include <math.h>
#include "fonts/builtin_font.h"
#include "font_store.h"

#include <render/ImageRenderer.h>
#include <render/PageRenderer.h>
#include <algorithm>
#include <new>
#include "../convert/sd_convert.h"
#include <SDCardManager.h>
#include <strings.h>
#include <text/hyph_en_us.h>
#include "engine/hyph_ru.h"

#include "../platform/hal/battery.h"
#include "../platform/hal/display.h"
#include "../platform/hal/mem_stats.h"
#include "../core/progress_sync.h"
#include "../services/sync_clock.h"
#include "../ui/font.h"
#include "../ui/i18n.h"
#include "../core/ui_port.h"
#include "config.h"
#include "../platform/device.h"
#include "engine/engine_text_counter.h"
#include "../core/epub_cfi.h"
#include "../ui/text_utils.h"

using namespace freeink::book;


namespace ReaderInternal {

// Working memory (FREEINK_BOOK_SMALL). On the X4 after boot the heap's biggest
// free block is ~96 KB and the next ~12 KB, so no two big buffers are ever live
// together: a deflated chapter is first inflated to the card on its own
// (kInflateBytes), then laid out from there as a stored entry, whose parse
// state is small (kStoredParseBytes) and sits next to the layout buffer.
constexpr size_t kLayoutBytes = 54 * 1024;        // layout state + image pre-scan

// The page index and anchor table of a chapter being written get an arena of
// their own, not the layout's: ChapterLayout marks and releases its scratch
// around placing an image, and a chapter's first page made in there had its
// index chunk released and written over (link targets in the page index:
// "Stale" pages). ~1.5 KB of anchors, ~1 KB per 128 pages, room to spare.
inline size_t writerBytesFor(uint32_t chapterBytes, int attempt = 0) {
    return 1600 + 1040 * (2 + chapterBytes / 128000 + 2 * attempt);
}
constexpr size_t kStoredParseBytes = 10 * 1024;   // XML parse of a stored chapter (2.6 KB seen)
constexpr size_t kInflateBytes = 48 * 1024;
// Most the transient buffers may hold together (the X4's free heap after boot is
// ~140 KB, its biggest block ~96 KB): a page or image that would go past it while
// a background job holds its ~64 KB stops (or finishes) the job first.
constexpr size_t kTransientBudget = 96 * 1024;       // one inflate stream (window + decompressor)
constexpr size_t kIndexScratchBytes = 24 * 1024;  // container index build (more for huge books, retried)
// How far the retries above may grow is the memory tier's (platform/tiers/*.h); the
// first sizes are the same everywhere, so page caches stay valid across devices.
constexpr size_t kRenderBytes = 12 * 1024;      // one page's runs and text (first try)
constexpr size_t kImageBytes = 52 * 1024;       // inflate + decode of a page's images
constexpr uint32_t kSpineFileMagic = 0x53424946;   // "FIBS"
// Until a chapter is laid out: rough page and character yields of XHTML bytes.
constexpr float kCharsPerByte = 0.55f;
constexpr float kDefaultBytesPerPage = 2600.0f;
// A CFI more than this far from the record's own percent is another position.
constexpr float kCfiTrustTolerance = 0.10f;

// RAII malloc for the transient buffers; tracks how much of them is live at once
// (BookReader::peakTransientBytes: the tests hold it under the X4's free block).
inline size_t g_liveTransient = 0, g_peakTransient = 0;
inline uint8_t* takeTransient(size_t size) {
    uint8_t* p = (uint8_t*)malloc(size);
    if (!p) return nullptr;
    g_liveTransient += size;
    if (g_liveTransient > g_peakTransient) g_peakTransient = g_liveTransient;
    return p;
}

inline void giveTransient(uint8_t*& p, size_t size) {
    if (!p) return;
    free(p);
    p = nullptr;
    g_liveTransient -= size;
}

struct Buffer {
    uint8_t* p = nullptr;
    size_t n = 0;
    explicit Buffer(size_t size) : p(takeTransient(size)), n(p ? size : 0) {}
    ~Buffer() { giveTransient(p, n); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

// One-time container index (ZIP directory, package, TOC, CSS) on the card.
inline bool buildContainerIndex(SdBookSource& src, SdCacheStorage& cache, bool& noMemory) {
    const uint32_t t0 = millis();
    BookStatus st = BookStatus::OutOfMemory;
    // Record tables grow with the book (~70 KB for a 1,700-chapter omnibus): retried larger.
    for (size_t cap = kIndexScratchBytes; st == BookStatus::OutOfMemory && cap <= Platform::kMemory.indexScratchMax; cap *= 2) {
        Buffer parse(kInflateBytes), scratch(cap);
        if (!scratch.p || !parse.p) {
            const MemStats::Snapshot m = MemStats::now();
            Serial.printf("[Reader] Not enough memory to index the book (%u + %u KB; free %u KB, largest %u KB)\n",
                          (unsigned)(kInflateBytes / 1024), (unsigned)(cap / 1024), (unsigned)(m.freeBytes / 1024),
                          (unsigned)(m.largestBlock / 1024));
            noMemory = true;
            return false;
        }
        Arena a(scratch.p, cap), b(parse.p, kInflateBytes);
        st = BookCatalog::build(src, cache, a, &b);
        Serial.printf("[Reader] Book index built: %s in %lu ms (scratch %u B of %u, parse %u B)\n", bookStatusName(st),
                      (unsigned long)(millis() - t0), (unsigned)a.highWater(), (unsigned)cap, (unsigned)b.highWater());
    }
    return st == BookStatus::Ok;
}

inline String stemOf(const String& path) {
    const int slash = path.lastIndexOf('/');
    const int dot = path.lastIndexOf('.');
    return path.substring(slash + 1, dot > slash ? dot : path.length());
}


// A page's pictures into the frame (as PageRenderer::renderImages, with a choice
// of how gray becomes black and white): Floyd-Steinberg error diffusion, or a
// plain threshold at mid-gray.
inline bool panelXY(const FrameTarget& t, int32_t x, int32_t y, int32_t& px, int32_t& py) {
    const bool tall = t.rotation == FrameRotation::Portrait || t.rotation == FrameRotation::PortraitInverted;
    const int32_t lw = tall ? t.height : t.width, lh = tall ? t.width : t.height;
    if (x < 0 || y < 0 || x >= lw || y >= lh) return false;
    switch (t.rotation) {
        case FrameRotation::Portrait: px = y; py = t.height - 1 - x; break;
        case FrameRotation::PortraitInverted: px = t.width - 1 - y; py = x; break;
        case FrameRotation::UpsideDown: px = t.width - 1 - x; py = t.height - 1 - y; break;
        case FrameRotation::None: px = x; py = y; break;
    }
    return true;
}

struct PictureBlit {
    const FrameTarget* t;
    int16_t x, y;
    bool sharp;
    int16_t* err;       // width+2, shifted by 1
    int16_t* errNext;
    uint16_t width;

    void black(int32_t lx, int32_t ly) const {
        int32_t px, py;
        if (panelXY(*t, lx, ly, px, py)) t->framebuffer[py * t->widthBytes + (px >> 3)] &= (uint8_t)~(0x80u >> (px & 7));
    }
    static bool onRow(void* user, uint16_t row, const uint8_t* gray, uint16_t w) {
        PictureBlit* b = static_cast<PictureBlit*>(user);
        if (w > b->width) w = b->width;
        if (b->sharp) {
            for (uint16_t i = 0; i < w; i++)
                if (gray[i] < 128) b->black(b->x + i, b->y + row);
            return true;
        }
        for (uint16_t i = 0; i < w; i++) {
            int32_t v = gray[i] + b->err[i + 1];
            v = v < 0 ? 0 : (v > 255 ? 255 : v);
            const bool ink = v < 128;
            if (ink) b->black(b->x + i, b->y + row);
            const int32_t r = v - (ink ? 0 : 255);
            b->err[i + 2] = (int16_t)(b->err[i + 2] + r * 7 / 16);
            b->errNext[i] = (int16_t)(b->errNext[i] + r * 3 / 16);
            b->errNext[i + 1] = (int16_t)(b->errNext[i + 1] + r * 5 / 16);
            b->errNext[i + 2] = (int16_t)(b->errNext[i + 2] + r / 16);
        }
        memcpy(b->err, b->errNext, (b->width + 2u) * sizeof(int16_t));
        memset(b->errNext, 0, (b->width + 2u) * sizeof(int16_t));
        return true;
    }
};

inline BookStatus drawImagesInto(const Page& page, BookSource& source, const ZipCatalog& zip, Arena& scratch,
                          const FrameTarget& frame, bool sharp) {
    BookStatus worst = BookStatus::Ok;
    for (uint16_t m = 0; m < page.imageCount; ++m) {
        const PageImage& image = page.images[m];
        const size_t marked = scratch.mark();
        PictureBlit blit{&frame, image.x, image.y, sharp, nullptr, nullptr, image.width};
        if (!sharp) {
            blit.err = scratch.allocArray<int16_t>(image.width + 2u);
            blit.errNext = scratch.allocArray<int16_t>(image.width + 2u);
            if (!blit.err || !blit.errNext) {
                scratch.release(marked);
                return BookStatus::OutOfMemory;
            }
            memset(blit.err, 0, (image.width + 2u) * sizeof(int16_t));
            memset(blit.errNext, 0, (image.width + 2u) * sizeof(int16_t));
        }
        const BookStatus st = ImageRenderer::render(source, zip, image, scratch, PictureBlit::onRow, &blit);
        scratch.release(marked);
        // An unsupported format leaves its space blank; a real error is reported.
        if (st != BookStatus::Ok && st != BookStatus::Unsupported) worst = st;
    }
    return worst;
}
}  // namespace ReaderInternal

using namespace ReaderInternal;

struct BookReader::Prelayout {
    enum class Phase : uint8_t { Unpack, Layout };
    Phase phase = Phase::Unpack;
    int spine = -1;
    uint32_t t0 = 0;
    uint32_t busyMs = 0;
    ZipEntry bookEntry;   // the chapter in the container (the unpacker points at it)
    ZipEntry entry;       // what the layout reads: bookEntry, or the unpacked file
    char href[256] = "";
    char name[48] = "";
    char unpackName[24] = "";
    SdCacheStorage cache;   // its own write stream: the foreground keeps _cache's
    SdBookSource chapterSrc;
    BookSource* source = nullptr;

    uint8_t* inflateMem = nullptr;
    Arena inflateArena;
    ZipEntryReader unpacker;

    uint8_t* layoutMem = nullptr;
    uint8_t* parseMem = nullptr;
    uint8_t* writerMem = nullptr;
    size_t writerBytes = 0;
    Arena layout;
    Arena parse;
    Arena writerArena;
    PageCacheWriter writer;
    ChapterLayoutSession session;
    bool writing = false;
    bool current = false;   // the chapter on screen (pages served from `writer`)

    ~Prelayout() {
        session.abort();
        if (writing) cache.abortWrite();
        chapterSrc.close();
        giveTransient(inflateMem, kInflateBytes);
        giveTransient(layoutMem, kLayoutBytes);
        giveTransient(parseMem, kStoredParseBytes);
        giveTransient(writerMem, writerBytes);
    }
};
