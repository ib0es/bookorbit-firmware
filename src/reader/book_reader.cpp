#include "book_reader.h"

#include <render/PageRenderer.h>
#include <algorithm>
#include <new>
#include "../convert/sd_convert.h"
#include <strings.h>
#include <text/hyph_en_us.h>
#include "engine/hyph_ru.h"

#include "../hal/battery.h"
#include "../hal/display.h"
#include "../hal/mem_stats.h"
#include "../services/progress_sync.h"
#include "../ui/font.h"
#include "../ui/i18n.h"
#include "../ui/kit/ui_kit.h"
#include "../ui/views/status_modal.h"
#include "config.h"
#include "engine/engine_text_counter.h"
#include "epub_cfi.h"

using namespace freeink::book;

BookReader Reader;

namespace {

// Working memory (FREEINK_BOOK_SMALL). On the X4 after boot the heap's biggest
// free block is ~96 KB and the next ~12 KB, so no two big buffers are ever live
// together: a deflated chapter is first inflated to the card on its own
// (kInflateBytes), then laid out from there as a stored entry, whose parse
// state is small (kStoredParseBytes) and sits next to the layout buffer.
constexpr size_t kLayoutBytes = 54 * 1024;        // layout state + page index chunks + image pre-scan
constexpr size_t kStoredParseBytes = 10 * 1024;   // XML parse of a stored chapter (2.6 KB seen)
constexpr size_t kInflateBytes = 48 * 1024;
// Most the transient buffers may hold together (the X4's free heap after boot is
// ~140 KB, its biggest block ~96 KB): a page or image that would go past it while
// a background job holds its ~64 KB stops (or finishes) the job first.
constexpr size_t kTransientBudget = 96 * 1024;       // one inflate stream (window + decompressor)
constexpr size_t kIndexScratchBytes = 24 * 1024;  // container index build (more for huge books, retried)
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
size_t g_liveTransient = 0, g_peakTransient = 0;
uint8_t* takeTransient(size_t size) {
    uint8_t* p = (uint8_t*)malloc(size);
    if (!p) return nullptr;
    g_liveTransient += size;
    if (g_liveTransient > g_peakTransient) g_peakTransient = g_liveTransient;
    return p;
}

void giveTransient(uint8_t*& p, size_t size) {
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
bool buildContainerIndex(SdBookSource& src, SdCacheStorage& cache, bool& noMemory) {
    const uint32_t t0 = millis();
    BookStatus st = BookStatus::OutOfMemory;
    // Record tables grow with the book (~70 KB for a 1,700-chapter omnibus): retried larger.
    for (size_t cap = kIndexScratchBytes; st == BookStatus::OutOfMemory && cap <= 96 * 1024; cap *= 2) {
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

String stemOf(const String& path) {
    const int slash = path.lastIndexOf('/');
    const int dot = path.lastIndexOf('.');
    return path.substring(slash + 1, dot > slash ? dot : path.length());
}

}  // namespace

// Background layout state (see tickBackground).
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
    Arena layout;
    Arena parse;
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
    }
};

size_t BookReader::peakTransientBytes(bool reset) {
    const size_t peak = g_peakTransient;
    if (reset) g_peakTransient = g_liveTransient;
    return peak;
}

String BookReader::cacheDirFor(const String& bookPath) {
    // The file name with its extension: "Name.epub" and "Name.fb2" are different books to lay out.
    return "/cache/" + bookPath.substring(bookPath.lastIndexOf('/') + 1) + ".fib";
}

BookReader::BookReader() : _fontSize(FontEngine::kDefaultSize) {
    _fonts.add(&_font);
    _hyphEn.init(k_hyph_en_us, k_hyph_en_us_size);
    _hyphRu.init(k_hyph_ru, k_hyph_ru_size);
}

BookReader::~BookReader() { closeBook(); }

const char* BookReader::getFontSizeName(int size) {
    static char buf[8][12];   // a few names may be alive at once (menus)
    static int next = 0;
    char* out = buf[next++ & 7];
    snprintf(out, sizeof(buf[0]), "%d pt", FontEngine::sizePx(size));
    return out;
}

// ------------------------------------------------------------------ open/close

bool BookReader::openBook(const String& filePath) {
    closeBook();
    _noMemory = false;
    const uint32_t t0 = millis();
    _path = filePath;
    String lower = filePath;
    lower.toLowerCase();
    _title = stemOf(filePath);
    String readPath = filePath;
    _convertStep = -1;
    _convertProblem = 0;
    if (convert::needsConversion(filePath)) {
        // Read through an EPUB made from it once, kept beside its page cache.
        convert::ConvertProblem why = convert::ConvertProblem::None;
        const bool converted = convert::ensureConverted(filePath, cacheDirFor(filePath), readPath,
                                                        &BookReader::drawConvertProgress, this, &_noMemory, &why);
        _convertProblem = (uint8_t)why;
        if (!converted) {
            Serial.printf("[Reader] Cannot convert %s\n", filePath.c_str());
            return false;
        }
    }
    if (!_src.open(readPath)) {
        Serial.printf("[Reader] Cannot open %s\n", filePath.c_str());
        return false;
    }
    Storage.mkdir("/cache");
    _cache.setDir(cacheDirFor(filePath));
    Storage.mkdir(_cache.dir().c_str());

    if (!openContainer()) {
        closeBook();
        return false;
    }
    setupLayout();
    loadSpineTable();
    buildToc();
    _spine = 0;
    _page = 0;
    _want = Want::Page;
    _wantValue = 0;
    _opened = true;
    uint64_t bytes = 0;
    for (const auto& s : _spines) bytes += s.bytes;
    Serial.printf("[Reader] Opened %s: %u spine items (%u KB), %u TOC entries in %lu ms\n", filePath.c_str(),
                  (unsigned)_spines.size(), (unsigned)(bytes / 1024), (unsigned)_toc.size(), (unsigned long)(millis() - t0));
    return true;
}

void BookReader::drawConvertProgress(void* self, int permille) {
    BookReader* r = static_cast<BookReader*>(self);
    // An e-ink refresh blocks for ~0.5-1 s: the window is drawn at the start, then
    // at most every 4 s, so that the drawing does not become the conversion's cost.
    const int step = permille / 100;
    if (permille >= 1000 || step == r->_convertStep) return;
    if (r->_convertStep >= 0 && millis() - r->_convertDrawnAt < 4000) return;
    r->_convertStep = step;
    char line[64];
    snprintf(line, sizeof(line), tr(MsgId::READER_CONVERT_PROGRESS_FMT), permille / 10);
    drawStatusModal(tr(MsgId::READER_CONVERT_TITLE), r->_title.substring(0, 32).c_str(), line);
    r->_convertDrawnAt = millis();
}

bool BookReader::openContainer() {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0 || !_cache.exists(BookCatalog::kCatalogName)) {
            if (!buildContainerIndex(_src, _cache, _noMemory)) return false;
        }
        size_t resident = 0;
        if (BookCatalog::residentBytes(_cache, &resident) != BookStatus::Ok) continue;
        free(_bookMem);
        _bookMem = (uint8_t*)malloc(resident + 64);
        Buffer scratch(4096);
        if (!_bookMem || !scratch.p) { _noMemory = true; return false; }
        _bookArena.init(_bookMem, resident + 64);
        Arena s(scratch.p, 4096);
        _catalog = BookCatalog();
        const BookStatus st = _catalog.open(_src, _cache, _bookArena, s);
        if (st == BookStatus::Ok) {
            if (_catalog.metadata().title && *_catalog.metadata().title) _title = _catalog.metadata().title;
            _spines.assign(_catalog.spineCount(), Spine());
            for (size_t i = 0; i < _spines.size(); i++) _spines[i].bytes = _catalog.spineSize(i);
            return !_spines.empty();
        }
        Serial.printf("[Reader] Book index: %s\n", bookStatusName(st));
        if (st != BookStatus::Stale) return false;
    }
    return false;
}

const char* BookReader::bookLanguage() {
    const char* lang = _catalog.metadata().language;
    return (lang && *lang) ? lang : "en";
}

const freeink::book::Hyphenator* BookReader::hyphenatorFor(const char* language) const {
    auto is = [language](const char* code) {
        const size_t n = strlen(code);
        return strncasecmp(language, code, n) == 0 && (language[n] == '\0' || language[n] == '-' || language[n] == '_');
    };
    if (is("ru") || is("rus")) return &_hyphRu;
    if (is("en") || is("eng")) return &_hyphEn;
    return nullptr;
}

void BookReader::setupLayout() {
    stopBackground();   // its pages would be of the old layout
    _bgNothingFor = -1;
    const BookPageGeometry geo = bookPageGeometry();
    LayoutParams p;
    p.pageWidth = (int16_t)Display.width();
    p.pageHeight = (int16_t)Display.height();
    p.marginLeft = (int16_t)geo.left;
    p.marginRight = (int16_t)(Display.width() - geo.left - geo.width);
    p.marginTop = (int16_t)geo.top;
    p.marginBottom = 56;   // the chapter line and progress bar below (rule at height - 44)
    p.baseSizePx = (uint16_t)FontEngine::sizePx(_fontSize);
    p.font = &_fonts;
    p.defaultAlign = freeink::book::TextAlign::Justify;
    p.language = bookLanguage();
    p.hyphenator = hyphenatorFor(p.language);
    p.stylesheet = _catalog.stylesheet();
    _params = p;
    _gen = layoutGenerationHash(_params, BitmapBookFont::kFingerprint);
    _held.spine = -1;   // pages of another layout
}

void BookReader::releaseIndex() {
    _pages = PageCacheReader();
    free(_indexMem);
    _indexMem = nullptr;
    _indexCap = 0;
    _loadedSpine = -1;
}

void BookReader::closeBook() {
    stopBackground();
    _bgNothingFor = -1;
    releaseIndex();
    _catalog = BookCatalog();
    free(_bookMem);
    _bookMem = nullptr;
    _src.close();
    _chapterSrc.close();
    std::vector<Spine>().swap(_spines);
    std::vector<ChapterItem>().swap(_toc);
    std::vector<uint32_t>().swap(_tocFragment);
    _opened = false;
    _want = Want::None;
    _cfiSpine = -1;
    _cfiChars = UINT32_MAX;
    _cfi = String();
}

// ------------------------------------------------------------- spine table

void BookReader::loadSpineTable() {
    for (auto& s : _spines) { s.pages = -1; s.chars = 0; }
    char name[32];
    snprintf(name, sizeof(name), "pages-%08x.bin", (unsigned)_gen);
    const uint32_t n = (uint32_t)_spines.size();
    if (_cache.fileSize(name) != (int64_t)(8 + 8 * n)) return;
    uint32_t head[2];
    if (_cache.readAt(name, 0, head, 8) != 8 || head[0] != kSpineFileMagic || head[1] != n) return;
    for (uint32_t i = 0; i < n; i++) {
        int32_t rec[2];
        if (_cache.readAt(name, 8 + 8 * i, rec, 8) != 8) return;
        _spines[i].pages = rec[0];
        _spines[i].chars = (uint32_t)rec[1];
    }
}

void BookReader::saveSpineTable() {
    char name[32];
    snprintf(name, sizeof(name), "pages-%08x.bin", (unsigned)_gen);
    if (!_cache.beginWrite(name)) return;
    const uint32_t head[2] = {kSpineFileMagic, (uint32_t)_spines.size()};
    bool ok = _cache.write(head, 8);
    for (const auto& s : _spines) {
        const int32_t rec[2] = {s.pages, (int32_t)s.chars};
        ok = ok && _cache.write(rec, 8);
    }
    _cache.endWrite();
    (void)ok;
}

float BookReader::pagesPerByte() const {
    // The chapters laid out so far, plus a prior worth kPriorBytes of default
    // yield (scaled by the font size, 16 px reference): a tiny chapter (a title
    // page, 2 pages from 700 bytes) must not make the book 2,500 pages long.
    constexpr float kPriorBytes = 64.0f * 1024.0f;
    const float px = (float)FontEngine::sizePx(_fontSize);
    const float prior = (px * px) / (16.0f * 16.0f) / kDefaultBytesPerPage;
    uint64_t pages = 0, bytes = 0;
    for (const auto& s : _spines) {
        if (s.pages >= 0) { pages += (uint64_t)s.pages; bytes += s.bytes; }
    }
    return ((float)pages + prior * kPriorBytes) / ((float)bytes + kPriorBytes);
}

int BookReader::pagesOf(int spine) const {
    if (spine < 0 || spine >= (int)_spines.size()) return 0;
    const Spine& s = _spines[spine];
    if (s.pages >= 0) return s.pages;
    int est = (int)((float)s.bytes * pagesPerByte() + 0.5f);
    if (building() && _bg->spine == spine && _bg->session.bytesConsumed() > 0) {
        // Being laid out: the pages made so far, extrapolated over the rest of its bytes.
        const uint32_t made = _bg->writer.pageCount();
        est = (int)((double)made * (double)_bg->session.bytesTotal() / (double)_bg->session.bytesConsumed() + 0.5);
        if (est < (int)made) est = (int)made;
    }
    return est < 1 ? 1 : est;
}

uint32_t BookReader::charsOf(int spine) const {
    if (spine < 0 || spine >= (int)_spines.size()) return 0;
    const Spine& s = _spines[spine];
    if (s.pages >= 0) return s.chars;
    if (building() && _bg->spine == spine) return curTotalChars();
    constexpr float kPriorBytes = 64.0f * 1024.0f;   // as in pagesPerByte()
    uint64_t chars = 0, bytes = 0;
    for (const auto& o : _spines) {
        if (o.pages >= 0) { chars += o.chars; bytes += o.bytes; }
    }
    const float perByte = ((float)chars + kCharsPerByte * kPriorBytes) / ((float)bytes + kPriorBytes);
    return (uint32_t)((float)s.bytes * perByte);
}

int BookReader::globalPage(int spine, int page) const {
    int n = 0;
    for (int s = 0; s < spine && s < (int)_spines.size(); s++) n += pagesOf(s);
    return n + page;
}

// ------------------------------------------------------------------- layout

// A chapter's bytes as a stored entry: the book itself when the ZIP stores it,
// else its inflated copy "x<spine>.xhtml" in the cache directory (made on first
// use with one inflate stream and nothing else allocated).
bool BookReader::chapterSource(int spine, ZipEntry& entry, BookSource*& source, SdBookSource& unpacked) {
    if (_catalog.spineEntry((size_t)spine, &entry) != BookStatus::Ok) return false;
    if (entry.method == 0) {
        source = &_src;
        return true;
    }
    char name[24];
    snprintf(name, sizeof(name), "x%d.xhtml", spine);
    const String path = _cache.dir() + "/" + name;
    if (_cache.fileSize(name) != (int64_t)entry.uncompressedSize) {
        stopBackground();   // the inflate stream needs the memory a background job holds
        const uint32_t t0 = millis();
        Buffer mem(kInflateBytes);
        if (!mem.p) return false;
        Arena arena(mem.p, kInflateBytes);
        ZipEntryReader reader;
        if (reader.open(_src, entry, arena) != BookStatus::Ok) return false;
        if (!_cache.beginWrite(name)) return false;
        uint8_t buf[1024];
        bool ok = true;
        for (int32_t n; ok && (n = reader.read(buf, sizeof(buf))) > 0;) ok = _cache.write(buf, (uint32_t)n);
        ok = _cache.endWrite() && ok && reader.totalProduced() == entry.uncompressedSize;
        Serial.printf("[Reader] Chapter %d unpacked: %u bytes in %lu ms%s\n", spine, (unsigned)entry.uncompressedSize,
                      (unsigned long)(millis() - t0), ok ? "" : " FAILED");
        if (!ok) { _cache.remove(name); return false; }
    }
    if (!unpacked.open(path)) return false;
    entry = ZipEntryReader::rawEntry(entry.uncompressedSize);
    source = &unpacked;
    return true;
}

bool BookReader::layoutChapter(int spine) {
    stopBackground();   // the layout buffers need the memory a background job holds
    const uint32_t t0 = millis();
    const Spine& sp = _spines[spine];
    if (sp.bytes > 60000) {   // a big one: say why the screen waits
        drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _title.substring(0, 32).c_str(), tr(MsgId::READER_FORMAT_LINE),
                        tr(MsgId::READER_OPENING_WAIT));
    }
    ZipEntry entry;
    BookSource* chapter = &_src;
    char href[256] = "";
    if (!chapterSource(spine, entry, chapter, _chapterSrc) ||
        _catalog.spineHref((size_t)spine, href, sizeof(href)) != BookStatus::Ok) {
        Serial.printf("[Reader] Chapter %d cannot be read\n", spine);
        return false;
    }
    // The page index grows ~1 KB per 128 pages inside the layout buffer: a whole
    // novel in one file needs more than the usual 54 KB, so a full buffer is
    // retried bigger (the parse state is small enough to sit beside it).
    char name[48];
    pageCacheName((uint16_t)spine, _gen, name, sizeof(name));
    uint32_t pages = 0, chars = 0;
    BookStatus st = BookStatus::OutOfMemory;
    size_t layoutUsed = 0, parseUsed = 0;
    PageCacheWriter writer;
    for (size_t cap = kLayoutBytes; st == BookStatus::OutOfMemory && cap <= kLayoutBytes + 32 * 1024; cap += 16 * 1024) {
        Buffer layoutMem(cap);
        Buffer parseMem(kStoredParseBytes);
        if (!layoutMem.p || !parseMem.p) {
            const MemStats::Snapshot m = MemStats::now();
            Serial.printf("[Reader] Not enough memory to lay out chapter %d (%u KB): free %u KB, largest block %u KB\n",
                          spine, (unsigned)(cap / 1024), (unsigned)(m.freeBytes / 1024), (unsigned)(m.largestBlock / 1024));
            break;
        }
        Arena layout(layoutMem.p, cap);
        Arena parse(parseMem.p, kStoredParseBytes);
        writer = PageCacheWriter();
        if (!writer.begin(_cache, name, _gen, layout)) {
            Serial.printf("[Reader] Cannot write the page cache %s/%s\n", _cache.dir().c_str(), name);
            st = BookStatus::IoError;
            break;
        }
        {
            // The image pre-scan's transient inflate runs in the layout buffer before layout starts.
            ChapterLayoutSession session;
            st = session.begin(_src, &_catalog.zip(), *chapter, entry, href, _params, layout, writer, &parse, &layout);
            while (st == BookStatus::Ok && !session.done()) st = session.step(1u << 30);
            pages = session.pagesEmitted();
            chars = session.totalChars();
            session.abort();
        }
        layoutUsed = layout.highWater();
        parseUsed = parse.highWater();
        if (st == BookStatus::Ok) {
            writer.setTotalChars(chars);
            if (!writer.finish() || writer.failed()) st = BookStatus::IoError;
        } else if (st == BookStatus::OutOfMemory) {
            Serial.printf("[Reader] Chapter %d: layout buffer of %u KB full, retrying bigger\n", spine, (unsigned)(cap / 1024));
            writer.finish();   // closes the half-built file...
            _cache.remove(name);   // ...which must not pass for a laid-out chapter
        }
    }
    _chapterSrc.close();
    const bool ok = st == BookStatus::Ok;
    Serial.printf("[Reader] Chapter %d laid out: %s, %u pages, %u chars in %lu ms (layout %u B, parse %u B)\n", spine,
                  ok ? "ok" : bookStatusName(st), (unsigned)pages, (unsigned)chars, (unsigned long)(millis() - t0),
                  (unsigned)layoutUsed, (unsigned)parseUsed);
    if (!ok) return false;
    commitLaidOut(spine, pages, chars);
    return true;
}

void BookReader::commitLaidOut(int spine, uint32_t pages, uint32_t chars) {
    _spines[spine].pages = (int32_t)pages;
    _spines[spine].chars = chars;
    saveSpineTable();
}

bool BookReader::loadChapter(int spine) {
    if (spine < 0 || spine >= (int)_spines.size()) return false;
    if (_loadedSpine == spine) return true;
    releaseIndex();
    if (_bg) _bg->current = false;
    char name[48];
    pageCacheName((uint16_t)spine, _gen, name, sizeof(name));
    bool laidOut = false;
    if (!_cache.exists(name)) {
        // Shown while it is laid out: the first page comes up as soon as it is made,
        // the rest follows behind the reader (or when a page not made yet is asked for).
        if (!_spines[spine].bgFailed && buildIncrementally(spine)) return true;
        if (_bg && _bg->spine == spine) stopBackground();
        if (!_cache.exists(name)) {
            if (!layoutChapter(spine)) return false;
            laidOut = true;
        }
    }
    // The page index stays allocated while the chapter is read: sized to it.
    size_t cap = 2048;
    for (int attempt = 0; attempt < 10; attempt++) {
        _indexMem = (uint8_t*)malloc(cap);
        if (!_indexMem) return false;
        _indexCap = cap;
        _indexArena.init(_indexMem, cap);
        _pages = PageCacheReader();
        const BookStatus st = _pages.open(_cache, name, _gen, _indexArena);
        if (st == BookStatus::Ok) break;
        free(_indexMem);
        _indexMem = nullptr;
        if (st == BookStatus::OutOfMemory && cap < 512 * 1024) {
            cap *= 2;
            continue;
        }
        if (laidOut || !layoutChapter(spine)) return false;   // stale or broken: lay it out again
        laidOut = true;
        cap = 2048;
    }
    if (!_indexMem) return false;
    _loadedSpine = spine;
    Spine& s = _spines[spine];
    if (s.pages != (int32_t)_pages.pageCount() || s.chars != _pages.totalChars()) {
        s.pages = (int32_t)_pages.pageCount();
        s.chars = _pages.totalChars();
        saveSpineTable();
    }
    return true;
}

bool BookReader::resolve() {
    if (!loadChapter(_spine)) return false;
    // A chapter without pages (an empty or image-less placeholder): the next one.
    while (curCount() == 0) {
        const bool back = _want == Want::LastPage;
        const int next = back ? _spine - 1 : _spine + 1;
        if (next < 0 || next >= (int)_spines.size()) return false;
        _spine = next;
        if (!loadChapter(_spine)) return false;
    }
    // While the chapter is still being laid out, only as much as the target needs.
    switch (_want) {
        case Want::Char: buildUntilChar(_wantValue); break;
        case Want::Page: buildUntilPages(_wantValue + 1); break;
        case Want::Anchor: buildUntilAnchor(_wantValue); break;
        case Want::LastPage:
        case Want::Fraction: if (building()) finishBackground(); break;   // needs the chapter's end
        case Want::None: buildUntilPages((uint32_t)_page + 1); break;
    }
    if (_loadedSpine != _spine && !loadChapter(_spine)) return false;   // the job failed: laid out in full
    const int count = (int)curCount();
    if (count == 0) return false;
    switch (_want) {
        case Want::Char: _page = (int)curPageForChar(_wantValue); break;
        case Want::Page: _page = (int)_wantValue; break;
        case Want::LastPage: _page = count - 1; break;
        case Want::Anchor: {
            uint32_t at = 0;
            _page = curCharForAnchor(_wantValue, &at) ? (int)curPageForChar(at) : 0;
            break;
        }
        case Want::Fraction: {
            const uint32_t total = curTotalChars();
            _page = (int)curPageForChar((uint32_t)((double)total * _wantFraction + 0.5));
            break;
        }
        case Want::None: break;
    }
    _want = Want::None;
    _held.spine = -1;
    if (_page < 0) _page = 0;
    if (_page >= count) _page = count - 1;
    return true;
}

bool BookReader::layoutRemaining(int maxChapters) {
    if (!_opened) return true;
    int done = 0;
    for (int s = 0; s < (int)_spines.size(); s++) {
        if (_spines[s].pages >= 0) continue;
        char name[48];
        pageCacheName((uint16_t)s, _gen, name, sizeof(name));
        if (_cache.exists(name) && s != _loadedSpine) {
            // Laid out before, but the table lost it: read its counts.
            const int keep = _loadedSpine;
            if (loadChapter(s) && keep >= 0 && keep != s) loadChapter(keep);
            continue;
        }
        if (done++ >= maxChapters) return false;
        if (!layoutChapter(s)) return false;
    }
    return true;
}

// ------------------------------------------------------------------ cover

bool BookReader::extractCover(const String& epubPath, const String& outPath, bool* noMemory, const String& cacheDir) {
    if (noMemory) *noMemory = false;
    bool oom = false;
    SdBookSource src;
    if (!src.open(epubPath)) return false;
    SdCacheStorage cache;
    Storage.mkdir("/cache");
    cache.setDir(cacheDir.length() ? cacheDir : cacheDirFor(epubPath));
    Storage.mkdir(cache.dir().c_str());
    // The book's index (built once, then used when the book is opened too).
    BookCatalog catalog;
    BookStatus st = BookStatus::Stale;
    size_t resident = 0;
    uint8_t* mem = nullptr;
    for (int attempt = 0; attempt < 2 && st == BookStatus::Stale; attempt++) {
        if ((attempt > 0 || !cache.exists(BookCatalog::kCatalogName)) && !buildContainerIndex(src, cache, oom)) break;
        if (BookCatalog::residentBytes(cache, &resident) != BookStatus::Ok) continue;
        free(mem);
        mem = (uint8_t*)malloc(resident + 64);
        Buffer scratch(4096);
        if (!mem || !scratch.p) { oom = true; break; }
        Arena bookArena(mem, resident + 64), s(scratch.p, 4096);
        catalog = BookCatalog();
        st = catalog.open(src, cache, bookArena, s);
    }
    bool ok = false;
    ZipEntry entry;
    if (st == BookStatus::Ok && catalog.coverEntry(&entry)) {
        // A stored image (the usual case) is copied; a deflated one needs an inflate stream.
        Buffer inflate(entry.method == 0 ? 1024 : kInflateBytes);
        if (!inflate.p) {
            oom = true;
        } else {
            Arena a(inflate.p, inflate.n);
            ZipEntryReader reader;
            Storage.mkdir("/covers");
            const String tmp = outPath + ".tmp";
            FsFile out = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
            if (out && reader.open(src, entry, a) == BookStatus::Ok) {
                uint8_t buf[1024];
                ok = true;
                for (int32_t n; ok && (n = reader.read(buf, sizeof(buf))) > 0;) ok = out.write(buf, (size_t)n) == (size_t)n;
                ok = ok && reader.totalProduced() == entry.uncompressedSize;
            }
            if (out) out.close();
            ok = ok && Storage.replaceFile(tmp.c_str(), outPath.c_str());
            if (!ok) Storage.remove(tmp.c_str());
        }
    }
    catalog = BookCatalog();
    free(mem);
    src.close();
    Serial.printf("[Reader] Cover of %s: %s\n", epubPath.c_str(),
                  ok ? outPath.c_str() : oom ? "no memory" : st != BookStatus::Ok ? "unreadable book" : "none");
    if (noMemory) *noMemory = oom;
    return ok;
}

// ------------------------------------------------------ background layout

int BookReader::nextToPrelayout() const {
    for (int s = _spine + 1; s <= _spine + kLookahead && s < (int)_spines.size(); s++) {
        const Spine& sp = _spines[s];
        if (sp.pages >= 0 || sp.bgFailed) continue;
        char name[48];
        pageCacheName((uint16_t)s, _gen, name, sizeof(name));
        if (const_cast<SdCacheStorage&>(_cache).exists(name)) continue;   // laid out, counts read when opened
        return s;
    }
    return -1;
}

bool BookReader::tickBackground() {
    if (!_opened) return false;
    if (_bg) {
        stepBackground();
        return true;
    }
    if (_bgNothingFor == _spine) return false;
    const int s = nextToPrelayout();
    if (s < 0 || !startBackground(s)) {
        _bgNothingFor = _spine;   // looked again once the reader moves to another chapter
        return false;
    }
    return true;
}

void BookReader::stopBackground() {
    if (!_bg) return;
    if (_bg->current) {
        // The chapter on screen loses its pages: come back to the same text when it is laid out again.
        // By page number: laid out again it gives the same pages, and an image page
        // shares its character position with the page after it.
        if (_want == Want::None && _loadedSpine == _bg->spine && _bg->writer.pageCount() > 0) {
            _want = Want::Page;
            _wantValue = (uint32_t)_page;
            _held.spine = _spine;
            _held.page = (uint32_t)_page;
            _held.chars = _bg->writer.charStart((uint32_t)_page);
        }
        _loadedSpine = -1;
    }
    Serial.printf("[Reader] Background layout of chapter %d stopped\n", _bg->spine);
    delete _bg;
    _bg = nullptr;
}

bool BookReader::startBackground(int spine) {
    const uint32_t t0 = millis();
    Prelayout* job = new (std::nothrow) Prelayout();
    if (!job) return false;
    job->spine = spine;
    job->t0 = t0;
    job->cache.setDir(_cache.dir());
    pageCacheName((uint16_t)spine, _gen, job->name, sizeof(job->name));
    if (_catalog.spineEntry((size_t)spine, &job->bookEntry) != BookStatus::Ok ||
        _catalog.spineHref((size_t)spine, job->href, sizeof(job->href)) != BookStatus::Ok) {
        _spines[spine].bgFailed = true;
        delete job;
        return false;
    }
    _bg = job;
    snprintf(job->unpackName, sizeof(job->unpackName), "x%d.xhtml", spine);
    if (job->bookEntry.method == 0) {
        job->entry = job->bookEntry;
        job->source = &_src;
        job->phase = Prelayout::Phase::Layout;
    } else if (_cache.fileSize(job->unpackName) == (int64_t)job->bookEntry.uncompressedSize) {
        job->phase = Prelayout::Phase::Layout;   // unpacked before (an earlier, stopped job)
    } else {
        job->inflateMem = takeTransient(kInflateBytes);
        if (!job->inflateMem) {
            stopBackground();
            return false;
        }
        job->inflateArena.init(job->inflateMem, kInflateBytes);
        if (job->unpacker.open(_src, job->bookEntry, job->inflateArena) != BookStatus::Ok ||
            !job->cache.beginWrite(job->unpackName)) {
            _spines[spine].bgFailed = true;
            stopBackground();
            return false;
        }
        job->writing = true;
    }
    Serial.printf("[Reader] Background layout of chapter %d started\n", spine);
    job->busyMs += millis() - t0;
    return true;
}

bool BookReader::stepBackground() {
    Prelayout* job = _bg;
    if (!job) return false;
    const uint32_t t0 = millis();
    bool failed = false;
    bool finished = false;
    if (job->phase == Prelayout::Phase::Unpack) {
        // ~8 KB of output per slice: a few tens of milliseconds.
        uint8_t buf[1024];
        for (int i = 0; i < 8; i++) {
            const int32_t n = job->unpacker.read(buf, sizeof(buf));
            if (n < 0 || (n > 0 && !job->cache.write(buf, (uint32_t)n))) { failed = true; break; }
            if (n == 0) break;
        }
        if (!failed && job->unpacker.totalProduced() >= job->bookEntry.uncompressedSize) {
            job->writing = false;
            failed = !job->cache.endWrite() || job->unpacker.totalProduced() != job->bookEntry.uncompressedSize;
            giveTransient(job->inflateMem, kInflateBytes);
            if (!failed) {
                Serial.printf("[Reader] Chapter %d unpacked in the background: %u bytes\n", job->spine,
                              (unsigned)job->bookEntry.uncompressedSize);
                job->phase = Prelayout::Phase::Layout;
            }
        }
    } else if (!job->session.active()) {
        // Layout phase, first slice: buffers, the cache file and the parse.
        if (!job->source) {
            if (!job->chapterSrc.open(_cache.dir() + "/" + job->unpackName)) failed = true;
            job->entry = ZipEntryReader::rawEntry(job->bookEntry.uncompressedSize);
            job->source = &job->chapterSrc;
        }
        job->layoutMem = failed ? nullptr : takeTransient(kLayoutBytes);
        job->parseMem = job->layoutMem ? takeTransient(kStoredParseBytes) : nullptr;
        if (!job->parseMem) {
            // No memory now (the foreground holds it): try again from the next chapter on.
            if (!failed) _bgNothingFor = _spine;
            stopBackground();
            return false;
        }
        job->layout.init(job->layoutMem, kLayoutBytes);
        job->parse.init(job->parseMem, kStoredParseBytes);
        if (!failed && job->writer.begin(job->cache, job->name, _gen, job->layout)) {
            job->writing = true;
            const BookStatus st = job->session.begin(_src, &_catalog.zip(), *job->source, job->entry, job->href,
                                                     _params, job->layout, job->writer, &job->parse, &job->layout);
            failed = st != BookStatus::Ok;
            finished = !failed && job->session.done();
        } else {
            failed = true;
        }
    } else {
        failed = job->session.step(1) != BookStatus::Ok;
        finished = !failed && job->session.done();
    }
    job->busyMs += millis() - t0;
    if (finished) {
        const uint32_t pages = job->session.pagesEmitted();
        const uint32_t chars = job->session.totalChars();
        job->writer.setTotalChars(chars);
        job->writing = false;
        const bool ok = job->writer.finish() && !job->writer.failed();
        Serial.printf("[Reader] Chapter %d laid out in the background: %s, %u pages, %u chars, %lu ms of work in %lu ms "
                      "(layout %u B)\n", job->spine, ok ? "ok" : "write failed", (unsigned)pages, (unsigned)chars,
                      (unsigned long)job->busyMs, (unsigned long)(millis() - job->t0),
                      (unsigned)job->layout.highWater());
        const int spine = job->spine;
        const bool current = job->current;
        if (!ok) {
            job->current = false;
            if (current) {
                if (_want == Want::None && pages > 0) { _want = Want::Page; _wantValue = (uint32_t)_page; }
                _loadedSpine = -1;
            }
        }
        delete job;
        _bg = nullptr;
        if (ok) commitLaidOut(spine, pages, chars);
        else _spines[spine].bgFailed = true;
        if (ok && current) {
            _loadedSpine = -1;   // the same pages, now from the finished cache file
            loadChapter(spine);
        }
        return false;
    }
    if (failed) {
        // A chapter too big for the background buffer (or unreadable): the foreground
        // lays it out when the reader gets there (or now, if it is on screen), with its retries.
        Serial.printf("[Reader] Background layout of chapter %d gave up\n", job->spine);
        _spines[job->spine].bgFailed = true;
        stopBackground();
        return false;
    }
    return true;
}

bool BookReader::finishBackground() {
    if (!_bg) return false;
    const uint64_t total = _bg->bookEntry.uncompressedSize;
    const uint64_t left = _bg->session.active() ? total - _bg->session.bytesConsumed() : total;
    Serial.printf("[Reader] Chapter %d: rest of the layout done now (%u KB of text left)\n", _bg->spine,
                  (unsigned)(left / 1024));
    if (left > 60000) {
        drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _title.substring(0, 32).c_str(), tr(MsgId::READER_FORMAT_LINE),
                        tr(MsgId::READER_OPENING_WAIT));
    }
    while (stepBackground()) {}
    return true;
}

bool BookReader::building() const { return _bg && _bg->current; }

void BookReader::makeRoom() {
    if (!_bg) return;
    if (!building()) {
        stopBackground();
        return;
    }
    // The chapter on screen: dropping the job costs laying it out again up to this
    // page (on the next turn); finishing it costs the rest of the chapter now.
    const int made = (int)_bg->writer.pageCount();
    const int left = pagesOf(_bg->spine) - made;
    if (_page + 1 < left) {
        Serial.printf("[Reader] Chapter %d: layout dropped for the page's images (%d of ~%d pages made)\n", _bg->spine,
                      made, made + left);
        stopBackground();
    } else {
        finishBackground();
    }
}

bool BookReader::buildIncrementally(int spine) {
    if (!_bg || _bg->spine != spine) {
        stopBackground();
        ZipEntry e;
        char unpacked[24];
        snprintf(unpacked, sizeof(unpacked), "x%d.xhtml", spine);
        if (_catalog.spineEntry((size_t)spine, &e) == BookStatus::Ok && e.method != 0 && e.uncompressedSize > 200000 &&
            _cache.fileSize(unpacked) != (int64_t)e.uncompressedSize) {
            drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _title.substring(0, 32).c_str(), tr(MsgId::READER_FORMAT_LINE),
                            tr(MsgId::READER_OPENING_WAIT));   // unpacking a big one takes seconds
        }
        if (!startBackground(spine)) return false;
    }
    _bg->current = true;
    _loadedSpine = spine;
    const uint32_t t0 = millis();
    buildUntilPages(1);
    if (building()) {
        Serial.printf("[Reader] Chapter %d: first page in %lu ms, the rest is laid out behind the reader\n", spine,
                      (unsigned long)(millis() - t0));
    }
    return _loadedSpine == spine;
}

// Catching up far into a chapter (its job was stopped for a sync or sleep, or a
// jump into a long one) takes seconds: the screen says so.
void BookReader::sayCatchingUp(uint32_t pagesLeft) {
    if (pagesLeft < 40) return;
    Serial.printf("[Reader] Chapter %d: laying out ~%u pages to get there\n", _bg->spine, (unsigned)pagesLeft);
    drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _title.substring(0, 32).c_str(), tr(MsgId::READER_FORMAT_LINE),
                    tr(MsgId::READER_OPENING_WAIT));
}

void BookReader::buildUntilPages(uint32_t count) {
    if (building() && _bg->writer.pageCount() < count) sayCatchingUp(count - _bg->writer.pageCount());
    while (building() && _bg->writer.pageCount() < count) stepBackground();
}

void BookReader::buildUntilChar(uint32_t chars) {
    if (building()) {
        const uint32_t n = _bg->writer.pageCount();
        const uint32_t made = n > 0 ? _bg->writer.charStart(n - 1) : 0;
        if (chars > made) sayCatchingUp((chars - made) / 1500);   // ~1500 characters a page
    }
    // The page holding `chars` is complete once a later page starts after it.
    while (building()) {
        const uint32_t n = _bg->writer.pageCount();
        if (n > 0 && _bg->writer.charStart(n - 1) > chars) break;
        stepBackground();
    }
}

void BookReader::buildUntilAnchor(uint32_t idHash) {
    uint32_t at = 0;
    while (building() && !_bg->writer.charForAnchor(idHash, &at)) stepBackground();
    if (building()) buildUntilChar(at);
}

uint32_t BookReader::curCount() const { return building() ? _bg->writer.pageCount() : _pages.pageCount(); }

uint32_t BookReader::curCharStart(uint32_t page) const {
    return building() ? _bg->writer.charStart(page) : _pages.charStart(page);
}

uint32_t BookReader::curPageForChar(uint32_t chars) const {
    return building() ? _bg->writer.pageForChar(chars) : _pages.pageForChar(chars);
}

bool BookReader::curCharForAnchor(uint32_t idHash, uint32_t* chars) const {
    return building() ? _bg->writer.charForAnchor(idHash, chars) : _pages.charForAnchor(idHash, chars);
}

uint32_t BookReader::curTotalChars() const {
    if (!building()) return _pages.totalChars();
    // Not known before the end: the characters so far, extrapolated over the bytes left.
    const uint32_t n = _bg->writer.pageCount();
    const uint64_t done = _bg->session.bytesConsumed(), total = _bg->session.bytesTotal();
    const uint32_t made = n > 0 ? _bg->writer.charStart(n - 1) : 0;
    if (done == 0) return made;
    return (uint32_t)((double)made * (double)total / (double)done);
}

// ---------------------------------------------------------------- position

BookReader::Position BookReader::position() const {
    Position p;
    if (heldHere()) {   // not worth laying the chapter out again for
        p.spine = _spine;
        p.chars = _held.chars;
        return p;
    }
    settle();
    p.spine = _spine;
    if (_want == Want::Char) p.chars = _wantValue;
    else if (_want == Want::Fraction) p.chars = (uint32_t)(_wantFraction * charsOf(_spine));
    else if (_want == Want::None && _loadedSpine == _spine) p.chars = curCharStart((uint32_t)_page);
    return p;
}

bool BookReader::goTo(const Position& pos) {
    if (!_opened || pos.spine < 0 || pos.spine >= (int)_spines.size()) return false;
    _spine = pos.spine;
    _want = Want::Char;
    _wantValue = pos.chars;
    return true;
}

float BookReader::getProgress() const {
    if (!_opened) return 0.0f;
    const Position here = position();
    uint64_t before = 0, total = 0;
    for (int s = 0; s < (int)_spines.size(); s++) {
        if (s < here.spine) before += _spines[s].bytes;
        total += _spines[s].bytes;
    }
    double inChapter = 0.0;
    if (_want == Want::Fraction) inChapter = _wantFraction;
    else if (_loadedSpine == here.spine && curTotalChars() > 0) inChapter = (double)here.chars / curTotalChars();
    else if (charsOf(here.spine) > 0) inChapter = (double)here.chars / charsOf(here.spine);
    if (inChapter > 1.0) inChapter = 1.0;
    const double at = (double)before + inChapter * (double)_spines[here.spine].bytes;
    return total > 0 ? (float)(at / (double)total) : 0.0f;
}

int BookReader::getCurrentPage() const {
    if (!_opened) return 1;
    if (heldHere()) return globalPage(_spine, (int)_wantValue) + 1;
    settle();
    const int page = (_want == Want::None) ? _page : 0;
    return globalPage(_spine, page) + 1;
}

int BookReader::getTotalPages() const {
    int n = 0;
    for (int s = 0; s < (int)_spines.size(); s++) n += pagesOf(s);
    return n > 0 ? n : 1;
}

bool BookReader::setPage(int page) {
    if (!_opened || _spines.empty()) return false;
    int left = page - 1;
    if (left < 0) left = 0;
    for (int s = 0; s < (int)_spines.size(); s++) {
        const int n = pagesOf(s);
        if (left < n || s == (int)_spines.size() - 1) {
            _spine = s;
            _want = Want::Page;
            _wantValue = (uint32_t)(left < n ? left : (n > 0 ? n - 1 : 0));
            return true;
        }
        left -= n;
    }
    return false;
}

bool BookReader::setPageByProgress(float percent) {
    if (!_opened || _spines.empty()) return false;
    if (percent > 1) percent = 1;
    if (!(percent > 0)) {   // the very start, the cover page included
        _spine = 0;
        _want = Want::Page;
        _wantValue = 0;
        return true;
    }
    uint64_t total = 0;
    for (const auto& s : _spines) total += s.bytes;
    double target = (double)total * percent;
    for (int s = 0; s < (int)_spines.size(); s++) {
        const double b = _spines[s].bytes;
        // The boundary itself (within float precision) is the next chapter's start.
        if (target < b - 0.5 || s == (int)_spines.size() - 1) {
            _spine = s;
            _want = Want::Fraction;
            _wantFraction = b > 0 ? (float)(target / b) : 0.0f;
            if (_wantFraction > 1.0f) _wantFraction = 1.0f;
            return true;
        }
        target -= b;
    }
    return false;
}

bool BookReader::setPageByXPath(const String& xpath, float fallbackPercent) {
    const int at = xpath.indexOf("DocFragment[");
    if (!_opened || at < 0) return false;
    const int spine = xpath.substring(at + 12).toInt() - 1;
    if (spine < 0 || spine >= (int)_spines.size()) return false;
    // The xpath names the chapter; the percent places it inside when it agrees.
    if (ProgressSync::hasProgress(fallbackPercent) && setPageByProgress(fallbackPercent) && _spine == spine) return true;
    _spine = spine;
    _want = Want::Page;
    _wantValue = 0;
    return true;
}

String BookReader::getXPath() const {
    if (!_opened) return "";
    return "/body/DocFragment[" + String(_spine + 1) + "]";
}

bool BookReader::goToPosition(const String& cfi, const String& xpath, float percent, int page) {
    if (!_opened) return false;
    if (cfi.length() > 0 && setPageByCfi(cfi, percent)) return true;
    if (xpath.length() > 0 && setPageByXPath(xpath, percent)) return true;
    if (ProgressSync::hasProgress(percent)) return setPageByProgress(percent);
    if (page > 1) return setPage(page);
    _spine = 0;
    _want = Want::Page;
    _wantValue = 0;
    return true;
}

// --------------------------------------------------------------------- CFI

bool BookReader::walkChapter(int spine, EpubCfi::Walker& walker) {
    ZipEntry entry;
    BookSource* source = nullptr;
    if (!chapterSource(spine, entry, source, _chapterSrc)) return false;
    Buffer mem(4096);   // a stored entry: no inflate state
    bool ok = mem.p != nullptr;
    if (ok) {
        Arena arena(mem.p, 4096);
        ZipEntryReader reader;
        ok = reader.open(*source, entry, arena) == BookStatus::Ok;
        char buf[512];
        for (int32_t n; ok && !walker.done() && (n = reader.read(buf, sizeof(buf))) > 0;) walker.feed(buf, (size_t)n);
        walker.finish();
    }
    _chapterSrc.close();
    return ok;
}

bool BookReader::cfiToChapterChar(const String& cfi, int& spine, uint32_t& chars) {
    EpubCfi::Location loc;
    if (!EpubCfi::parse(cfi.c_str(), loc)) return false;
    spine = loc.ordinal - 1;
    if (spine < 0 || spine >= (int)_spines.size()) return false;
    EngineTextCounter counter;
    EpubCfi::Walker walker = EpubCfi::Walker::toOffset(counter, loc.steps, loc.offset);
    if (!walkChapter(spine, walker)) return false;
    chars = walker.textOffset();
    return walker.reached();
}

bool BookReader::chapterCharToCfi(int spine, uint32_t chars, String& out) {
    EngineTextCounter counter;
    EpubCfi::Walker walker = EpubCfi::Walker::toLocation(counter, chars);
    if (!walkChapter(spine, walker) || !walker.reached()) return false;
    EpubCfi::Location loc = walker.location();
    loc.ordinal = spine + 1;
    out = EpubCfi::format(loc).c_str();
    return true;
}

bool BookReader::setPageByCfi(const String& cfi, float fallbackPercent) {
    if (!_opened || cfi.length() == 0) return false;
    int spine = 0;
    uint32_t chars = 0;
    if (!cfiToChapterChar(cfi, spine, chars)) {
        Serial.printf("[CFI] '%s': not usable in this book\n", cfi.c_str());
        return false;
    }
    if (ProgressSync::hasProgress(fallbackPercent)) {
        uint64_t before = 0, total = 0;
        for (int s = 0; s < (int)_spines.size(); s++) {
            if (s < spine) before += _spines[s].bytes;
            total += _spines[s].bytes;
        }
        const uint32_t inChapter = charsOf(spine);
        const double frac = inChapter > 0 ? (double)chars / inChapter : 0.0;
        const float at = total > 0 ? (float)(((double)before + (frac > 1 ? 1 : frac) * _spines[spine].bytes) / (double)total)
                                   : 0.0f;
        if (fabsf(at - fallbackPercent) > kCfiTrustTolerance) {
            Serial.printf("[CFI] '%s' points to %.1f%% but progress is %.1f%%: stale, not used\n", cfi.c_str(),
                          at * 100.0f, fallbackPercent * 100.0f);
            return false;
        }
    }
    Serial.printf("[CFI] '%s' -> spine %d, char %u\n", cfi.c_str(), spine, (unsigned)chars);
    _spine = spine;
    _want = Want::Char;
    _wantValue = chars;
    return true;
}

String BookReader::getCfi() {
    if (!_opened) return "";
    const Position p = position();
    if (p.spine == _cfiSpine && p.chars == _cfiChars) return _cfi;
    String cfi;
    // A chapter's start (its first page may be an image: a cover, a title plate)
    // is its <body>, not the first text character a page or two later.
    if (p.chars == 0 || !chapterCharToCfi(p.spine, p.chars, cfi)) {
        // Nothing to point at past this character (an image page): the chapter's body.
        cfi = "epubcfi(/6/" + String((p.spine + 1) * 2) + "!/4)";
    }
    _cfiSpine = p.spine;
    _cfiChars = p.chars;
    _cfi = cfi;
    return cfi;
}

// --------------------------------------------------------------------- TOC

void BookReader::buildToc() {
    _toc.clear();
    _tocFragment.clear();
    char title[96], frag[96];
    for (size_t i = 0; i < _catalog.tocCount(); i++) {
        BookCatalog::TocItem item;
        if (_catalog.tocItem(i, &item, title, sizeof(title), frag, sizeof(frag)) != BookStatus::Ok) continue;
        if (item.spineIndex < 0) continue;
        ChapterItem c;
        c.title = title;
        c.byteOffset = (uint32_t)item.spineIndex;
        c.page = 1;
        _toc.push_back(c);
        _tocFragment.push_back(item.hasFragment ? ZipCatalog::hashPath(frag) : 0);
    }
}

const std::vector<ChapterItem>& BookReader::getChapters() {
    for (auto& c : _toc) c.page = globalPage((int)c.byteOffset, 0) + 1;   // estimates move as chapters get laid out
    return _toc;
}

int BookReader::getCurrentChapterIndex() const {
    int idx = -1;
    const Position here = position();
    for (int i = 0; i < (int)_toc.size(); i++) {
        const int spine = (int)_toc[i].byteOffset;
        if (spine > here.spine) break;
        if (spine == here.spine && _tocFragment[i] != 0 && _loadedSpine == spine) {
            uint32_t at = 0;
            if (curCharForAnchor(_tocFragment[i], &at) && at > here.chars) break;
        }
        idx = i;
    }
    return idx;
}

String BookReader::getCurrentChapterTitle() const {
    const int idx = getCurrentChapterIndex();
    return idx >= 0 ? _toc[idx].title : String();
}

bool BookReader::goToChapter(int chapterIndex) {
    if (!_opened || chapterIndex < 0 || chapterIndex >= (int)_toc.size()) return false;
    _spine = (int)_toc[chapterIndex].byteOffset;
    if (_tocFragment[chapterIndex] != 0) {
        _want = Want::Anchor;
        _wantValue = _tocFragment[chapterIndex];
    } else {
        _want = Want::Page;
        _wantValue = 0;
    }
    return true;
}

bool BookReader::nextChapter() {
    const int idx = getCurrentChapterIndex();
    if (!goToChapter(idx + 1)) return false;
    renderCurrentPage();
    return true;
}

bool BookReader::prevChapter() {
    const int idx = getCurrentChapterIndex();
    if (idx < 0) return false;
    // Inside a chapter: back to its start; at its start: the one before.
    const Position before = position();
    goToChapter(idx);
    if (resolve() && position().spine == before.spine && position().chars >= before.chars && idx > 0) goToChapter(idx - 1);
    renderCurrentPage();
    return true;
}

// ------------------------------------------------------------------- pages

bool BookReader::nextPage() {
    if (!_opened || !resolve()) return false;
    buildUntilPages((uint32_t)_page + 2);
    if (_loadedSpine != _spine && !resolve()) return false;
    if (_page + 1 < (int)curCount()) {
        _page++;
    } else {
        if (_spine + 1 >= (int)_spines.size()) return false;
        _spine++;
        _want = Want::Page;
        _wantValue = 0;
    }
    renderCurrentPage();
    return true;
}

bool BookReader::prevPage() {
    if (!_opened || !resolve()) return false;
    if (_page > 0) {
        _page--;
    } else {
        if (_spine == 0) return false;
        _spine--;
        _want = Want::LastPage;
    }
    renderCurrentPage();
    return true;
}

bool BookReader::setFontSize(int size) {
    size = FontEngine::clampSize(size);
    if (size == _fontSize) return false;
    const Position here = position();
    _fontSize = size;
    if (_opened) {
        // A new generation: other page caches, the same sentence.
        releaseIndex();
        setupLayout();
        loadSpineTable();
        goTo(here);
    }
    return true;
}

// ------------------------------------------------------------------ render

void BookReader::drawStatusBar() {
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    const int16_t left = 20, right = (int16_t)(width() - 20);
    char statusRight[64];
    snprintf(statusRight, sizeof(statusRight), "%d/%d  |  %d%%", getCurrentPage(), getTotalPages(),
             Battery.getPercentage());
    const int16_t rw = (int16_t)(textWidth(FontSmall, statusRight) + 4);
    Rect topBand{left, 4, (int16_t)(right - left), 24};
    text(Rect{(int16_t)(right - rw), topBand.y, rw, topBand.height}, statusRight, FontSmall, Color::Black,
         uikit::TextAlign::Right);
    text(Rect{left, topBand.y, (int16_t)(topBand.width - rw - 12), topBand.height}, _title.c_str(), FontSmall);
    hline(left, right, 30);
    const int16_t bottomRule = (int16_t)(height() - 44);
    hline(left, right, bottomRule);
    String chTitle = getCurrentChapterTitle();
    const int32_t permille = (int32_t)(getProgress() * 1000.0f);
    if (chTitle.length() > 0) {
        text(Rect{left, (int16_t)(bottomRule + 2), (int16_t)(right - left), 20}, chTitle.c_str(), FontSmall);
        progress(f, Rect{left, (int16_t)(height() - 18), (int16_t)(right - left), 8}, permille);
    } else {
        progress(f, Rect{left, (int16_t)(height() - 28), (int16_t)(right - left), 10}, permille);
    }
}

void BookReader::renderCurrentPage() {
    if (!_opened) return;
    const uint32_t t0 = millis();
    if (!resolve()) {
        Serial.printf("[Reader] Chapter %d cannot be shown (layout failed or no memory)\n", _spine);
        Display.clear(EPD_WHITE);
        drawStatusBar();
        Display.update(REFRESH_PARTIAL);
        return;
    }
    const uint32_t tFound = millis();
    Serial.printf("[Reader] Page %d/%d (chapter %d, page %d/%u)\n", getCurrentPage(), getTotalPages(), _spine,
                  _page + 1, (unsigned)curCount());
    Display.clear(EPD_WHITE);
    drawStatusBar();
    const uint32_t tBar = millis();
    uint32_t tRead = 0, tText = 0;
    unsigned imageCount = 0;
    // A page record: runs plus text; justified text is many runs. Retried larger
    // when the page does not fit (dense pages need ~15 KB).
    for (size_t cap = kRenderBytes; cap <= 64 * 1024; cap *= 2) {
        if (_bg && (g_liveTransient + cap > kTransientBudget || MemStats::now().largestBlock < cap + 4096)) {
            Serial.printf("[Reader] Page record of %u B does not fit beside the layout job (held %u B, largest block %u B)\n",
                          (unsigned)cap, (unsigned)g_liveTransient, (unsigned)MemStats::now().largestBlock);
            if (building()) finishBackground();   // the page on screen comes from it: finished, not dropped
            else stopBackground();
        }
        Buffer mem(cap);
        if (!mem.p) {
            Serial.printf("[Reader] No memory to draw the page (%u B)\n", (unsigned)cap);
            _renderFailures++;
            break;
        }
        Arena arena(mem.p, cap);
        Page page{};
        const BookStatus st = building() ? _bg->writer.readPage((uint32_t)_page, arena, &page)
                                         : _pages.readPage((uint32_t)_page, arena, &page);
        if (st == BookStatus::OutOfMemory && cap < 64 * 1024) continue;
        if (st != BookStatus::Ok) {
            _renderFailures++;
            Serial.printf("[Reader] Page %d of chapter %d unreadable: %s\n", _page + 1, _spine, bookStatusName(st));
            break;
        }
        FrameTarget frame{Display.getBuffer(), (int16_t)EPD_WIDTH, (int16_t)EPD_HEIGHT, (int16_t)(EPD_WIDTH / 8),
                          FrameFormat::Mono1Sharp, FrameRotation::Portrait};
        uint32_t firstMissing = 0;
        tRead = millis();
        const uint32_t missing = PageRenderer::renderText(page, _fonts, frame, &firstMissing);
        tText = millis();
        imageCount = page.imageCount;
        if (missing) Serial.printf("[Reader] %u characters not in the font (first U+%04X)\n", (unsigned)missing,
                                   (unsigned)firstMissing);
        if (page.imageCount > 0) {
            auto drawImages = [&](size_t size) {
                Buffer img(size);
                if (!img.p) return BookStatus::OutOfMemory;
                Arena a(img.p, size);
                return PageRenderer::renderImages(page, _src, _catalog.zip(), a, frame);
            };
            // Smallest arena first: a PNG decodes in a few KB of arena but pngle takes
            // ~44 KB of heap of its own, so a big arena next to it would not fit; a
            // baseline JPEG needs ~30 KB of arena, a wide one up to kImageBytes.
            auto drawLadder = [&](size_t limit) {
                BookStatus r = BookStatus::OutOfMemory;
                for (size_t size : {(size_t)12 * 1024, (size_t)32 * 1024, kImageBytes}) {
                    if (size > limit) break;
                    r = drawImages(size);
                    if (r != BookStatus::OutOfMemory) break;
                }
                return r;
            };
            BookStatus ist = BookStatus::OutOfMemory;
            if (_bg) {
                // First with what is left beside the job.
                const size_t largest = MemStats::now().largestBlock;
                size_t room = kTransientBudget > g_liveTransient ? kTransientBudget - g_liveTransient : 0;
                room = std::min(room, std::min(kImageBytes, largest > 4096 ? largest - 4096 : 0));
                ist = drawLadder(room);
                if (ist == BookStatus::OutOfMemory) makeRoom();   // the page record is in our buffer: still valid
            }
            if (ist == BookStatus::OutOfMemory) ist = drawLadder(kImageBytes);
            if (ist == BookStatus::OutOfMemory) {
                const MemStats::Snapshot m = MemStats::now();
                Serial.printf("[Reader] No memory for the page's images (free %u KB, largest block %u KB)\n",
                              (unsigned)(m.freeBytes / 1024), (unsigned)(m.largestBlock / 1024));
            } else if (ist != BookStatus::Ok) {
                Serial.printf("[Reader] Page's images not drawn: %s\n", bookStatusName(ist));
            }
        }
        break;
    }
    const bool full = (_renderedPages++ % FULL_REFRESH_EVERY_PAGES) == 0;
    const uint32_t tEnd = millis();
    // Where the time goes: finding the page (layout, cache), its record, the text, the images.
    Serial.printf("[Reader] Page drawn in %lu ms (find %lu, status bar %lu, record %lu, text %lu, %u images %lu)\n",
                  (unsigned long)(tEnd - t0), (unsigned long)(tFound - t0), (unsigned long)(tBar - tFound),
                  (unsigned long)(tRead ? tRead - tBar : 0),
                  (unsigned long)(tText ? tText - tRead : 0), (unsigned)imageCount,
                  (unsigned long)(tText ? tEnd - tText : 0));
    Display.update(full ? REFRESH_FULL : REFRESH_PARTIAL);
}
