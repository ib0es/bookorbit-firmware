#include "book_reader.h"

#include <render/PageRenderer.h>
#include <text/hyph_en_us.h>

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

// Working memory for laying out one chapter (FREEINK_BOOK_SMALL, measured by
// fibcheck on 27 real books: layout <= 50 KB with the page index, parse 46 KB).
constexpr size_t kLayoutBytes = 56 * 1024;
constexpr size_t kParseBytes = 48 * 1024;
constexpr size_t kRenderBytes = 8 * 1024;       // one page's runs and text
constexpr size_t kImageBytes = 52 * 1024;       // inflate + decode of a page's images
constexpr uint32_t kSpineFileMagic = 0x53424946;   // "FIBS"
// Until a chapter is laid out: rough page and character yields of XHTML bytes.
constexpr float kCharsPerByte = 0.55f;
constexpr float kDefaultBytesPerPage = 2600.0f;
// A CFI more than this far from the record's own percent is another position.
constexpr float kCfiTrustTolerance = 0.10f;

// RAII malloc for the transient buffers.
struct Buffer {
    uint8_t* p = nullptr;
    explicit Buffer(size_t n) : p((uint8_t*)malloc(n)) {}
    ~Buffer() { free(p); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

String stemOf(const String& path) {
    const int slash = path.lastIndexOf('/');
    const int dot = path.lastIndexOf('.');
    return path.substring(slash + 1, dot > slash ? dot : path.length());
}

}  // namespace

String BookReader::cacheDirFor(const String& bookPath) {
    // The file name with its extension: "Name.epub" and "Name.fb2" are different books to lay out.
    return "/cache/" + bookPath.substring(bookPath.lastIndexOf('/') + 1) + ".fib";
}

BookReader::BookReader() : _fontSize(FontEngine::kDefaultSize) {
    _fonts.add(&_font);
    _hyph.init(k_hyph_en_us, k_hyph_en_us_size);
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
    _isTxt = lower.endsWith(".txt");
    _title = stemOf(filePath);
    if (!_src.open(filePath)) {
        Serial.printf("[Reader] Cannot open %s\n", filePath.c_str());
        return false;
    }
    Storage.mkdir("/cache");
    _cache.setDir(cacheDirFor(filePath));
    Storage.mkdir(_cache.dir().c_str());

    if (_isTxt) {
        _spines.assign(1, Spine());
        _spines[0].bytes = (uint32_t)_src.size();
    } else if (!openContainer()) {
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

bool BookReader::openContainer() {
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt > 0 || !_cache.exists(BookCatalog::kCatalogName)) {
            // One-time container index (ZIP directory, package, TOC, CSS) on the card.
            const uint32_t t0 = millis();
            Buffer scratch(kLayoutBytes), parse(kParseBytes);
            if (!scratch.p || !parse.p) {
                Serial.println("[Reader] Not enough memory to index the book");
                _noMemory = true;
                return false;
            }
            Arena a(scratch.p, kLayoutBytes), b(parse.p, kParseBytes);
            const BookStatus st = BookCatalog::build(_src, _cache, a, &b);
            Serial.printf("[Reader] Book index built: %s in %lu ms (scratch %u B, parse %u B)\n", bookStatusName(st),
                          (unsigned long)(millis() - t0), (unsigned)a.highWater(), (unsigned)b.highWater());
            if (st != BookStatus::Ok) return false;
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

void BookReader::setupLayout() {
    const BookPageGeometry geo = bookPageGeometry();
    LayoutParams p;
    p.pageWidth = (int16_t)Display.width();
    p.pageHeight = (int16_t)Display.height();
    p.marginLeft = (int16_t)geo.left;
    p.marginRight = (int16_t)(Display.width() - geo.left - geo.width);
    p.marginTop = (int16_t)geo.top;
    p.marginBottom = 48;   // the chapter line and progress bar below
    p.baseSizePx = (uint16_t)FontEngine::sizePx(_fontSize);
    p.font = &_fonts;
    p.defaultAlign = freeink::book::TextAlign::Justify;
    const char* lang = (!_isTxt && _catalog.metadata().language) ? _catalog.metadata().language : "";
    p.language = *lang ? lang : "en";
    // Only English patterns ship with the engine for now.
    p.hyphenator = (strncmp(p.language, "en", 2) == 0) ? &_hyph : nullptr;
    p.stylesheet = _isTxt ? nullptr : _catalog.stylesheet();
    _params = p;
    _gen = layoutGenerationHash(_params, BitmapBookFont::kFingerprint);
}

void BookReader::releaseIndex() {
    _pages = PageCacheReader();
    free(_indexMem);
    _indexMem = nullptr;
    _indexCap = 0;
    _loadedSpine = -1;
}

void BookReader::closeBook() {
    releaseIndex();
    _catalog = BookCatalog();
    free(_bookMem);
    _bookMem = nullptr;
    _src.close();
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
    const int est = (int)((float)s.bytes * pagesPerByte() + 0.5f);
    return est < 1 ? 1 : est;
}

uint32_t BookReader::charsOf(int spine) const {
    if (spine < 0 || spine >= (int)_spines.size()) return 0;
    const Spine& s = _spines[spine];
    if (s.pages >= 0) return s.chars;
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

bool BookReader::layoutChapter(int spine) {
    const uint32_t t0 = millis();
    const Spine& sp = _spines[spine];
    if (sp.bytes > 60000 || _isTxt) {   // a big one: say why the screen waits
        drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _title.substring(0, 32).c_str(), tr(MsgId::READER_FORMAT_LINE),
                        tr(MsgId::READER_OPENING_WAIT));
    }
    // The big buffers first, while the heap's biggest block is still whole.
    Buffer layoutMem(kLayoutBytes);
    Buffer parseMem(_isTxt ? 0 : kParseBytes);
    if (!layoutMem.p || (!_isTxt && !parseMem.p)) {
        const MemStats::Snapshot m = MemStats::now();
        Serial.printf("[Reader] Not enough memory to lay out chapter %d: free %u KB, largest block %u KB\n", spine,
                      (unsigned)(m.freeBytes / 1024), (unsigned)(m.largestBlock / 1024));
        return false;
    }
    Arena layout(layoutMem.p, kLayoutBytes);
    Arena parse(parseMem.p, _isTxt ? 0 : kParseBytes);
    char name[48];
    pageCacheName((uint16_t)spine, _gen, name, sizeof(name));
    PageCacheWriter writer;
    if (!writer.begin(_cache, name, _gen, layout)) {
        Serial.printf("[Reader] Cannot write the page cache %s/%s\n", _cache.dir().c_str(), name);
        return false;
    }
    uint32_t pages = 0, chars = 0;
    BookStatus st;
    if (_isTxt) {
        st = ChapterLayout::layoutPlainText(_src, _params, layout, writer, &pages, &chars);
    } else {
        ZipEntry entry;
        char href[256];
        st = _catalog.spineEntry((size_t)spine, &entry);
        if (st == BookStatus::Ok) st = _catalog.spineHref((size_t)spine, href, sizeof(href));
        if (st == BookStatus::Ok) {
            st = ChapterLayout::layout(_src, _catalog.zip(), entry, href, _params, layout, writer, &pages, &chars, &parse);
        }
    }
    writer.setTotalChars(chars);
    const bool ok = st == BookStatus::Ok && writer.finish() && !writer.failed();
    Serial.printf("[Reader] Chapter %d laid out: %s, %u pages, %u chars in %lu ms (layout %u B, parse %u B)\n", spine,
                  ok ? "ok" : bookStatusName(st), (unsigned)pages, (unsigned)chars, (unsigned long)(millis() - t0),
                  (unsigned)layout.highWater(), (unsigned)parse.highWater());
    if (!ok) return false;
    _spines[spine].pages = (int32_t)pages;
    _spines[spine].chars = chars;
    saveSpineTable();
    return true;
}

bool BookReader::loadChapter(int spine) {
    if (spine < 0 || spine >= (int)_spines.size()) return false;
    if (_loadedSpine == spine) return true;
    releaseIndex();
    char name[48];
    pageCacheName((uint16_t)spine, _gen, name, sizeof(name));
    bool laidOut = false;
    if (!_cache.exists(name)) {
        if (!layoutChapter(spine)) return false;
        laidOut = true;
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
    while (_pages.pageCount() == 0) {
        const bool back = _want == Want::LastPage;
        const int next = back ? _spine - 1 : _spine + 1;
        if (next < 0 || next >= (int)_spines.size()) return false;
        _spine = next;
        if (!loadChapter(_spine)) return false;
    }
    const int count = (int)_pages.pageCount();
    switch (_want) {
        case Want::Char: _page = (int)_pages.pageForChar(_wantValue); break;
        case Want::Page: _page = (int)_wantValue; break;
        case Want::LastPage: _page = count - 1; break;
        case Want::Anchor: {
            uint32_t at = 0;
            _page = _pages.charForAnchor(_wantValue, &at) ? (int)_pages.pageForChar(at) : 0;
            break;
        }
        case Want::Fraction: {
            const uint32_t total = _pages.totalChars();
            _page = (int)_pages.pageForChar((uint32_t)((double)total * _wantFraction + 0.5));
            break;
        }
        case Want::None: break;
    }
    _want = Want::None;
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

// ---------------------------------------------------------------- position

BookReader::Position BookReader::position() const {
    settle();
    Position p;
    p.spine = _spine;
    if (_want == Want::Char) p.chars = _wantValue;
    else if (_want == Want::Fraction) p.chars = (uint32_t)(_wantFraction * charsOf(_spine));
    else if (_want == Want::None && _loadedSpine == _spine) p.chars = _pages.charStart((uint32_t)_page);
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
    else if (_loadedSpine == here.spine && _pages.totalChars() > 0) inChapter = (double)here.chars / _pages.totalChars();
    else if (charsOf(here.spine) > 0) inChapter = (double)here.chars / charsOf(here.spine);
    if (inChapter > 1.0) inChapter = 1.0;
    const double at = (double)before + inChapter * (double)_spines[here.spine].bytes;
    return total > 0 ? (float)(at / (double)total) : 0.0f;
}

int BookReader::getCurrentPage() const {
    if (!_opened) return 1;
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
    if (!_opened || _isTxt || at < 0) return false;
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
    if (!_opened || _isTxt) return "";
    return "/body/DocFragment[" + String(_spine + 1) + "]";
}

bool BookReader::goToPosition(const String& cfi, const String& xpath, float percent, int page) {
    if (!_opened) return false;
    if (cfi.length() > 0 && !_isTxt && setPageByCfi(cfi, percent)) return true;
    if (xpath.length() > 0 && setPageByXPath(xpath, percent)) return true;
    if (ProgressSync::hasProgress(percent)) return setPageByProgress(percent);
    if (page > 1) return setPage(page);
    _spine = 0;
    _want = Want::Page;
    _wantValue = 0;
    return true;
}

// --------------------------------------------------------------------- CFI

bool BookReader::cfiToChapterChar(const String& cfi, int& spine, uint32_t& chars) {
    EpubCfi::Location loc;
    if (_isTxt || !EpubCfi::parse(cfi.c_str(), loc)) return false;
    spine = loc.ordinal - 1;
    if (spine < 0 || spine >= (int)_spines.size()) return false;
    ZipEntry entry;
    if (_catalog.spineEntry((size_t)spine, &entry) != BookStatus::Ok) return false;
    Buffer mem(kParseBytes);
    if (!mem.p) return false;
    Arena arena(mem.p, kParseBytes);
    ZipEntryReader reader;
    if (reader.open(_src, entry, arena) != BookStatus::Ok) return false;
    EngineTextCounter counter;
    EpubCfi::Walker walker = EpubCfi::Walker::toOffset(counter, loc.steps, loc.offset);
    char buf[512];
    for (int32_t n; !walker.done() && (n = reader.read(buf, sizeof(buf))) > 0;) walker.feed(buf, (size_t)n);
    walker.finish();
    chars = walker.textOffset();
    return walker.reached();
}

bool BookReader::chapterCharToCfi(int spine, uint32_t chars, String& out) {
    ZipEntry entry;
    if (_isTxt || _catalog.spineEntry((size_t)spine, &entry) != BookStatus::Ok) return false;
    Buffer mem(kParseBytes);
    if (!mem.p) return false;
    Arena arena(mem.p, kParseBytes);
    ZipEntryReader reader;
    if (reader.open(_src, entry, arena) != BookStatus::Ok) return false;
    EngineTextCounter counter;
    EpubCfi::Walker walker = EpubCfi::Walker::toLocation(counter, chars);
    char buf[512];
    for (int32_t n; !walker.done() && (n = reader.read(buf, sizeof(buf))) > 0;) walker.feed(buf, (size_t)n);
    walker.finish();
    if (!walker.reached()) return false;
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
    if (!_opened || _isTxt) return "";
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
    if (_isTxt) return;
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
            if (_pages.charForAnchor(_tocFragment[i], &at) && at > here.chars) break;
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
    if (_page + 1 < (int)_pages.pageCount()) {
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
    Serial.printf("[Reader] Page %d/%d (chapter %d, page %d/%u)\n", getCurrentPage(), getTotalPages(), _spine,
                  _page + 1, (unsigned)_pages.pageCount());
    Display.clear(EPD_WHITE);
    drawStatusBar();
    {
        Buffer mem(kRenderBytes);
        if (mem.p) {
            Arena arena(mem.p, kRenderBytes);
            Page page{};
            if (_pages.readPage((uint32_t)_page, arena, &page) == BookStatus::Ok) {
                FrameTarget frame{Display.getBuffer(), (int16_t)EPD_WIDTH, (int16_t)EPD_HEIGHT, (int16_t)(EPD_WIDTH / 8),
                                  FrameFormat::Mono1Sharp, FrameRotation::Portrait};
                uint32_t firstMissing = 0;
                const uint32_t missing = PageRenderer::renderText(page, _fonts, frame, &firstMissing);
                if (missing) Serial.printf("[Reader] %u characters not in the font (first U+%04X)\n", (unsigned)missing,
                                           (unsigned)firstMissing);
                if (page.imageCount > 0 && !_isTxt) {
                    Buffer img(kImageBytes);
                    if (img.p) {
                        Arena a(img.p, kImageBytes);
                        PageRenderer::renderImages(page, _src, _catalog.zip(), a, frame);
                    } else {
                        Serial.println("[Reader] No memory for the page's images");
                    }
                }
            }
        }
    }
    const bool full = (_renderedPages++ % FULL_REFRESH_EVERY_PAGES) == 0;
    Serial.printf("[Reader] Page drawn in %lu ms\n", (unsigned long)(millis() - t0));
    Display.update(full ? REFRESH_FULL : REFRESH_PARTIAL);
}
