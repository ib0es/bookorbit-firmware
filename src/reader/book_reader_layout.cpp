#include "book_reader_internal.h"

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
        Ui::status(tr(MsgId::READER_FORMAT_TITLE), utf8Prefix(_title, 64).c_str(), tr(MsgId::READER_FORMAT_LINE),
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
    // A full layout buffer (or page index: ~1 KB per 128 pages, a whole novel in
    // one file) is retried bigger (the parse state is small enough to sit beside it).
    char name[48];
    pageCacheName((uint16_t)spine, _gen, name, sizeof(name));
    uint32_t pages = 0, chars = 0;
    BookStatus st = BookStatus::OutOfMemory;
    size_t layoutUsed = 0, parseUsed = 0;
    PageCacheWriter writer;
    for (size_t cap = kLayoutBytes; st == BookStatus::OutOfMemory && cap <= kLayoutBytes + Platform::kMemory.layoutExtraMax; cap += 16 * 1024) {
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
        const size_t wcap = writerBytesFor(entry.uncompressedSize, (int)((cap - kLayoutBytes) / (16 * 1024)));
        Buffer writerMem(wcap);
        if (!writerMem.p) break;
        Arena writerArena(writerMem.p, wcap);
        writer = PageCacheWriter();
        if (!writer.begin(_cache, name, _gen, writerArena)) {
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
        job->writerBytes = writerBytesFor(job->bookEntry.uncompressedSize);
        job->writerMem = job->parseMem ? takeTransient(job->writerBytes) : nullptr;
        if (!job->writerMem) {
            // No memory now (the foreground holds it): try again from the next chapter on.
            if (!failed) _bgNothingFor = _spine;
            stopBackground();
            return false;
        }
        job->layout.init(job->layoutMem, kLayoutBytes);
        job->parse.init(job->parseMem, kStoredParseBytes);
        job->writerArena.init(job->writerMem, job->writerBytes);
        if (!failed && job->writer.begin(job->cache, job->name, _gen, job->writerArena)) {
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
        Ui::status(tr(MsgId::READER_FORMAT_TITLE), utf8Prefix(_title, 64).c_str(), tr(MsgId::READER_FORMAT_LINE),
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
            Ui::status(tr(MsgId::READER_FORMAT_TITLE), utf8Prefix(_title, 64).c_str(), tr(MsgId::READER_FORMAT_LINE),
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
    Ui::status(tr(MsgId::READER_FORMAT_TITLE), utf8Prefix(_title, 64).c_str(), tr(MsgId::READER_FORMAT_LINE),
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

