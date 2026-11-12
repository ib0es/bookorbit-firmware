#include "book_reader_internal.h"

BookReader Reader;


size_t BookReader::peakTransientBytes(bool reset) {
    const size_t peak = g_peakTransient;
    if (reset) g_peakTransient = g_liveTransient;
    return peak;
}

String BookReader::cacheKeyFor(const String& bookPath) {
    // The file name with its extension: "Name.epub" and "Name.fb2" are different books to lay out.
    const int slash = bookPath.lastIndexOf('/');
    const String name = bookPath.substring(slash + 1);
    const String dir = slash > 0 ? bookPath.substring(0, slash) : String();
    if (dir.length() == 0 || dir.equalsIgnoreCase("/books")) return name;   // as before: existing caches stay valid
    String key = dir.substring(dir.startsWith("/") ? 1 : 0);
    key.replace("/", "#");
    return key + "#" + name;
}

String BookReader::cacheDirFor(const String& bookPath) {
    return "/cache/" + cacheKeyFor(bookPath) + ".fib";
}

BookReader::BookReader() : _fontSize(FontEngine::kDefaultSize) {
    setupFonts("");
    _hyphEn.init(k_hyph_en_us, k_hyph_en_us_size);
    _hyphRu.init(k_hyph_ru, k_hyph_ru_size);
}

BookReader::~BookReader() { closeBook(); }

void BookReader::setupFonts(const String& family) {
    // stb's working buffers while a glyph is rasterized (a few KB): a background
    // layout gives the room if the heap has none.
    _ttf.setNoMemoryHandler([](void* self) {
        BookReader* r = static_cast<BookReader*>(self);
        Serial.println("[Reader] No memory to rasterize a glyph: making room");
        r->makeRoom();
    }, this);
    _fonts = freeink::book::FontChain();
    _ttf.clearFaces();
    _family = "";
    // An installed family when asked for, mapped from flash; else the built-in one.
    if (family.length() && FontStore::installedName() == family && FontStore::map()) {
        for (int style = 0; style < 4; style++) {
            uint32_t len = 0;
            const uint8_t* data = FontStore::face(style, &len);
            if (data && _ttf.addFace((uint8_t)style, data, len)) _fonts.add(_ttf.face((uint8_t)style), (uint8_t)style);
        }
        if (_ttf.face(0)) _family = family;
        else Serial.printf("[Fonts] %s: unreadable, the built-in family instead\n", family.c_str());
    }
    if (!_ttf.face(0)) {
        _ttf.clearFaces();
        _fonts = freeink::book::FontChain();
        for (int style = 0; style < 4; style++) {
            uint32_t len = 0;
            const uint8_t* data = builtinfont::face(style, &len);
            if (_ttf.addFace((uint8_t)style, data, len)) _fonts.add(_ttf.face((uint8_t)style), (uint8_t)style);
        }
    }
    _fonts.add(&_font);   // anything the family lacks
}

const char* BookReader::builtinFontName() { return builtinfont::name(); }

String BookReader::fontFamilyName() const { return _family.length() ? _family : String(builtinfont::name()); }

bool BookReader::setFontFamily(const String& name) {
    const String want = name == builtinfont::name() ? String() : name;
    if (want == _family) return false;
    const Position here = position();
    stopBackground();   // its layout is in the old font
    setupFonts(want);
    if (_opened) {
        releaseIndex();
        setupLayout();
        loadSpineTable();
        goTo(here);
    }
    return true;
}

// ------------------------------------------------------------------ open/close

bool BookReader::openBook(const String& filePath) {
    Platform::feedWatchdog();
    closeBook();
    _noMemory = false;
    _ttf.reserve();   // low in the heap, before the book's buffers come and go around it
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
        Serial.printf("[Reader] Cannot open %s (read as %s, on card: %d)\n", filePath.c_str(), readPath.c_str(),
                      (int)Storage.exists(readPath.c_str()));
        return false;
    }
    _cfiMap.clear();
    if (convert::needsConversion(filePath)) {
        const String mapPath = cacheDirFor(filePath) + "/" + convert::kCfiMapName;
        // Read whole (SDCardManager::readFile stops at 50 KB: a long FB2's map was cut).
        FsFile f = Storage.open(mapPath.c_str(), O_RDONLY);
        if (f) {
            std::string text(f.size(), '\0');
            const size_t got = text.empty() ? 0 : (size_t)f.read(&text[0], text.size());
            f.close();
            if (got != text.size() || !_cfiMap.parse(text.data(), text.size()))
                Serial.printf("[CFI] %s: not a map\n", mapPath.c_str());
        }
    }
    Storage.mkdir("/cache");
    _cache.setDir(cacheDirFor(filePath));
    Storage.mkdir(_cache.dir().c_str());

    if (!openContainer()) {
        // An index that could not be built or loaded for want of one big enough
        // block is a memory problem, not a broken book: it opens after a restart.
        const MemStats::Snapshot m = MemStats::now();
        if (m.largestBlock < kInflateBytes + kIndexScratchBytes) _noMemory = true;
        Serial.printf("[Reader] Cannot open the book's index (%s; free %u KB, largest block %u KB)\n",
                      _noMemory ? "short of memory" : "unreadable", (unsigned)(m.freeBytes / 1024),
                      (unsigned)(m.largestBlock / 1024));
        const bool noMemory = _noMemory;
        closeBook();
        _noMemory = noMemory;
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
    Platform::feedWatchdog();
    BookReader* r = static_cast<BookReader*>(self);
    // An e-ink refresh blocks for ~0.5-1 s: the window is drawn at the start, then
    // at most every 4 s, so that the drawing does not become the conversion's cost.
    const int step = permille / 100;
    if (permille >= 1000 || step == r->_convertStep) return;
    if (r->_convertStep >= 0 && millis() - r->_convertDrawnAt < 4000) return;
    r->_convertStep = step;
    char line[64];
    snprintf(line, sizeof(line), tr(MsgId::READER_CONVERT_PROGRESS_FMT), permille / 10);
    Ui::status(tr(MsgId::READER_CONVERT_TITLE), utf8Prefix(r->_title, 64).c_str(), line);
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
    p.pageWidth = (int16_t)(landscape() ? EPD_WIDTH : EPD_HEIGHT);
    p.pageHeight = (int16_t)(landscape() ? EPD_HEIGHT : EPD_WIDTH);
    (void)geo;
    p.marginLeft = (int16_t)_opts.marginPx;
    p.marginRight = (int16_t)_opts.marginPx;
    // Room for the status bar: its top line, and below the chapter line and progress
    // bar (rule at height - 44), the bar alone, or nothing.
    p.marginTop = (int16_t)(_bar.topShown() ? geo.top : 16);
    p.marginBottom = (int16_t)(_bar.bottom == 0 ? 56 : _bar.bottom == 1 ? 30 : 16);
    p.baseSizePx = (uint16_t)FontEngine::sizePx(_fontSize);
    p.font = &_fonts;
    p.defaultAlign = _opts.justify ? freeink::book::TextAlign::Justify : freeink::book::TextAlign::Left;
    p.language = bookLanguage();
    p.hyphenator = _opts.hyphenate ? hyphenatorFor(p.language) : nullptr;
    p.stylesheet = _opts.bookStyles ? _catalog.stylesheet() : nullptr;
    p.embeddedStyles = _opts.bookStyles;
    p.lineSpacingPct = _opts.lineSpacingPct;
    p.paragraphSpacingPct = _opts.paragraphSpacingPct;
    p.orphanLines = p.widowLines = _opts.keepLines ? 2 : 1;
    p.focusReading = _opts.focusReading;
    _params = p;
    _gen = layoutGenerationHash(_params, BitmapBookFont::kFingerprint ^ _ttf.fingerprint());
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
    _cfiMap.clear();
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

int BookReader::pagesOf(int spine) const { return pagesOf(spine, -1.0f); }

// `ppb` < 0: worked out here. The loops over every chapter (page numbers, the
// table of contents) pass it in: pagesPerByte() is itself a loop over every
// chapter, which made them quadratic (seconds for a book of a thousand parts).
int BookReader::pagesOf(int spine, float ppb) const {
    if (spine < 0 || spine >= (int)_spines.size()) return 0;
    const Spine& s = _spines[spine];
    if (s.pages >= 0) return s.pages;
    int est = (int)((float)s.bytes * (ppb >= 0.0f ? ppb : pagesPerByte()) + 0.5f);
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
    const float ppb = pagesPerByte();
    int n = 0;
    for (int s = 0; s < spine && s < (int)_spines.size(); s++) n += pagesOf(s, ppb);
    return n + page;
}

