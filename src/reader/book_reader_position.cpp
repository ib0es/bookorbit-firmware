#include "book_reader_internal.h"

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
    const float ppb = pagesPerByte();
    int n = 0;
    for (int s = 0; s < (int)_spines.size(); s++) n += pagesOf(s, ppb);
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


namespace {
// The chapter's text from an engine character on: tags left out, white space
// collapsed, the common entities as characters.
class TextFrom {
public:
    TextFrom(uint32_t from, size_t maxBytes) : _from(from), _limit(maxBytes) {}
    bool full() const { return _out.size() >= _limit; }
    const std::string& text() const { return _out; }
    void feed(const char* d, size_t n) {
        for (size_t i = 0; i < n && !full(); i++) {
            const char c = d[i];
            _counter.feed(&c, 1);
            if (c == '<') {
                _tag = true;
                continue;
            }
            if (_tag) {
                if (c == '>') {
                    _tag = false;
                    space();   // between elements: words stay apart
                }
                continue;
            }
            if (c == '&') {
                _entity.clear();
                _inEntity = true;
                continue;
            }
            if (_inEntity) {
                if (c != ';' && _entity.size() < 10) {
                    _entity += c;
                    continue;
                }
                _inEntity = false;
                if (_entity == "amp") put('&');
                else if (_entity == "lt") put('<');
                else if (_entity == "gt") put('>');
                else if (_entity == "quot") put('"');
                else space();
                continue;
            }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') space();
            else put(c);
        }
    }
private:
    // Character `_from` is the one that makes the count pass it.
    bool started() const { return _counter.emitted() > _from; }
    void space() {
        if (started() && !_out.empty() && _out.back() != ' ') _out += ' ';
    }
    void put(char c) {
        if (started()) _out += c;
    }
    EngineTextCounter _counter;
    uint32_t _from;
    size_t _limit;
    std::string _out, _entity;
    bool _tag = false, _inEntity = false;
};
}  // namespace

String BookReader::textAt(int spine, uint32_t chars, size_t maxBytes) {
    if (!_opened || spine < 0 || spine >= (int)_spines.size()) return String();
    ZipEntry entry;
    BookSource* source = nullptr;
    if (!chapterSource(spine, entry, source, _chapterSrc)) return String();
    // From the character before: when it is not a space the place is inside a
    // word, whose rest is left out.
    TextFrom grab(chars > 0 ? chars - 1 : 0, maxBytes + 40);
    Buffer mem(4096);
    if (mem.p) {
        Arena arena(mem.p, 4096);
        ZipEntryReader reader;
        if (reader.open(*source, entry, arena) == BookStatus::Ok) {
            char buf[512];
            for (int32_t n; !grab.full() && (n = reader.read(buf, sizeof(buf))) > 0;) grab.feed(buf, (size_t)n);
        }
    }
    _chapterSrc.close();
    std::string t = grab.text();
    if (chars > 0 && !t.empty() && t[0] != ' ') {
        const size_t sp = t.find(' ');
        t.erase(0, sp == std::string::npos ? t.size() : sp);
    }
    while (!t.empty() && t[0] == ' ') t.erase(0, 1);
    if (t.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(t[cut]) & 0xC0) == 0x80) cut--;   // a whole character
        t.resize(cut);
    }
    return String(t.c_str());
}

String BookReader::toPublicCfi(const String& local) const {
    if (_cfiMap.empty() || local.length() == 0) return local;
    EpubCfi::Location loc;
    if (!EpubCfi::parse(local.c_str(), loc) || !_cfiMap.toPublic(loc)) return local;
    return EpubCfi::format(loc).c_str();
}

String BookReader::toLocalCfi(const String& pub) const {
    if (_cfiMap.empty() || pub.length() == 0) return pub;
    EpubCfi::Location loc;
    if (!EpubCfi::parse(pub.c_str(), loc) || !_cfiMap.toLocal(loc)) return String();
    return EpubCfi::format(loc).c_str();
}

bool BookReader::cfiToChapterChar(const String& publicCfi, int& spine, uint32_t& chars) {
    const String cfi = toLocalCfi(publicCfi);
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
    const String cfi = cfiAt(p.spine, p.chars);
    _cfiSpine = p.spine;
    _cfiChars = p.chars;
    _cfi = cfi;
    return cfi;
}

String BookReader::cfiAt(int spine, uint32_t chars) {
    if (!_opened || spine < 0 || spine >= (int)_spines.size()) return "";
    String cfi;
    // A chapter's start (its first page may be an image: a cover, a title plate)
    // is its <body>, not the first text character a page or two later.
    if (chars == 0 || !chapterCharToCfi(spine, chars, cfi)) {
        // Nothing to point at past this character (an image page): the chapter's body.
        cfi = "epubcfi(/6/" + String((spine + 1) * 2) + "!/4)";
    }
    return toPublicCfi(cfi);
}

bool BookReader::placeOfCfi(const String& cfi, int& spine, uint32_t& chars) {
    return _opened && cfi.length() > 0 && cfiToChapterChar(cfi, spine, chars);
}

float BookReader::percentAt(int spine, uint32_t chars) const {
    if (!_opened || spine < 0 || spine >= (int)_spines.size()) return 0.0f;
    uint64_t before = 0, total = 0;
    for (int s = 0; s < (int)_spines.size(); s++) {
        if (s < spine) before += _spines[s].bytes;
        total += _spines[s].bytes;
    }
    const uint32_t inChapter = charsOf(spine);
    double frac = inChapter > 0 ? (double)chars / inChapter : 0.0;
    if (frac > 1.0) frac = 1.0;
    return total > 0 ? (float)(((double)before + frac * _spines[spine].bytes) / (double)total) : 0.0f;
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
    // Estimates move as chapters get laid out. Pages before each spine item, summed once.
    const float ppb = pagesPerByte();
    std::vector<int> before(_spines.size() + 1, 0);
    for (size_t s = 0; s < _spines.size(); s++) before[s + 1] = before[s] + pagesOf((int)s, ppb);
    for (auto& c : _toc) {
        const size_t s = std::min<size_t>((size_t)c.byteOffset, _spines.size());
        c.page = before[s] + 1;
    }
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

