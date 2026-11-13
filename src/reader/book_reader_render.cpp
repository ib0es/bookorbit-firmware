#include "book_reader_internal.h"
#include "../ui/kit/ui_kit.h"   // the page-problem notice, turning the UI target (KNOWN_EXCEPTIONS)

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

bool BookReader::atEnd() {
    if (!_opened || !resolve()) return false;
    buildUntilPages((uint32_t)_page + 2);
    if (_loadedSpine != _spine && !resolve()) return false;
    return _spine + 1 >= (int)_spines.size() && _page + 1 >= (int)curCount();
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

bool BookReader::pageRange(int& spine, uint32_t& from, uint32_t& to) const {
    if (!_opened || _want != Want::None || _loadedSpine != _spine) return false;
    spine = _spine;
    from = _page == 0 ? 0 : curCharStart((uint32_t)_page);   // the chapter's start belongs to its first page
    to = (uint32_t)_page + 1 < curCount() ? curCharStart((uint32_t)_page + 1) : 0xFFFFFFFFu;
    return true;
}

bool BookReader::setLayoutOptions(const LayoutOptions& options) {
    if (options == _opts) return false;
    const Position here = position();
    _opts = options;
    if (_opened) {
        releaseIndex();
        setupLayout();
        loadSpineTable();
        goTo(here);
    }
    return true;
}

bool BookReader::setOrientation(int orientation) {
    orientation &= 3;
    if (orientation == _orient) return false;
    const Position here = position();
    _orient = orientation;
    if (_opened) {
        releaseIndex();
        setupLayout();
        loadSpineTable();
        goTo(here);
    }
    return true;
}

bool BookReader::setStatusBar(const StatusBar& bar) {
    if (bar == _bar) return false;
    const bool relayout = bar.topShown() != _bar.topShown() || (bar.bottom == 0) != (_bar.bottom == 0) ||
                          (bar.bottom == 2) != (_bar.bottom == 2);
    _bar = bar;
    if (_opened && relayout) {
        const Position here = position();
        releaseIndex();
        setupLayout();
        loadSpineTable();
        goTo(here);
    }
    return true;
}

FrameRotation BookReader::frameRotation() const {
    switch (_orient) {
        case 1:
            return FrameRotation::None;
        case 2:
            return FrameRotation::PortraitInverted;
        case 3:
            return FrameRotation::UpsideDown;
        default:
            return FrameRotation::Portrait;
    }
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
    if (_bar.topShown()) {
        // right: page number, percent read, battery; left: the title or chapter
        String r;
        auto add = [&](const String& part) { r = r.length() ? r + "  |  " + part : part; };
        if (_bar.pages == 0) add(String(getCurrentPage()) + "/" + String(getTotalPages()));
        if (_bar.pages == 1) add(String(_page + 1) + "/" + String((unsigned)curCount()));
        if (_bar.percent) add(String((int)(getProgress() * 100.0f)) + "%");
        if (_bar.clock) {
            char clk[8];
            if (SyncClock::formatLocalTime(_bar.utcOffsetMin, clk, sizeof(clk))) add(String(clk));
        }
        Rect topBand{left, 4, (int16_t)(right - left), 24};
        int16_t rw = 0;
        if (_bar.battery) {   // the battery glyph with its percentage, at the right
            char pct[8];
            snprintf(pct, sizeof(pct), "%d%%+", Battery.getPercentage());
            const int16_t bw = (int16_t)(textWidth(FontSmall, pct) + 32);
            battery(f, Rect{(int16_t)(right - bw), topBand.y, bw, topBand.height}, Color::Black);
            rw = (int16_t)(bw + (r.length() ? 14 : 0));
        }
        if (r.length()) {
            const int16_t tw = (int16_t)(textWidth(FontSmall, r.c_str()) + 4);
            text(Rect{(int16_t)(right - rw - tw), topBand.y, tw, topBand.height}, r.c_str(), FontSmall, Color::Black,
                 uikit::TextAlign::Right);
            rw = (int16_t)(rw + tw);
        }
        const String l = _bar.top == 0 ? _title : _bar.top == 1 ? getCurrentChapterTitle() : String();
        if (l.length())
            text(Rect{left, topBand.y, (int16_t)(topBand.width - rw - 12), topBand.height}, l.c_str(), FontSmall);
        hline(left, right, 30);
        // A bookmarked page: a ribbon hanging from the top rule, left of the numbers.
        int bSpine;
        uint32_t bFrom, bTo;
        if (_bookmarkProbe && pageRange(bSpine, bFrom, bTo) && _bookmarkProbe(bSpine, bFrom, bTo)) {
            const int16_t bx = (int16_t)(right - rw - 22);
            fill(Rect{bx, 0, 12, 22}, Color::Black);
            for (int16_t i = 0; i < 6; i++)
                hline((int16_t)(bx + i), (int16_t)(bx + 12 - i), (int16_t)(22 - 6 + i), Color::White);
        }
    } else {
        // No top line: a bookmark still shows, in the corner.
        int bSpine;
        uint32_t bFrom, bTo;
        if (_bookmarkProbe && pageRange(bSpine, bFrom, bTo) && _bookmarkProbe(bSpine, bFrom, bTo)) {
            const int16_t bx = (int16_t)(right - 12);
            fill(Rect{bx, 0, 10, 14}, Color::Black);
            for (int16_t i = 0; i < 5; i++)
                hline((int16_t)(bx + i), (int16_t)(bx + 10 - i), (int16_t)(14 - 5 + i), Color::White);
        }
    }
    const int32_t permille = (int32_t)(getProgress() * 1000.0f);
    if (_bar.bottom == 0) {
        const int16_t bottomRule = (int16_t)(height() - 44);
        hline(left, right, bottomRule);
        String chTitle = getCurrentChapterTitle();
        if (chTitle.length() > 0) {
            text(Rect{left, (int16_t)(bottomRule + 2), (int16_t)(right - left), 20}, chTitle.c_str(), FontSmall);
            progress(f, Rect{left, (int16_t)(height() - 18), (int16_t)(right - left), 8}, permille);
        } else {
            progress(f, Rect{left, (int16_t)(height() - 28), (int16_t)(right - left), 10}, permille);
        }
    } else if (_bar.bottom == 1) {
        progress(f, Rect{left, (int16_t)(height() - 18), (int16_t)(right - left), 8}, permille);
    }
}

namespace {
// While a page is drawn the UI (its status bar, a modal on it) is turned as the
// page is; the interface's own turn after.
struct TurnedUi {
    freeink::ui::Orientation was;
    explicit TurnedUi(freeink::book::FrameRotation r) : was(uikit::target().orientation()) {
        using freeink::ui::Orientation;
        using freeink::book::FrameRotation;
        uikit::setOrientation(r == FrameRotation::None               ? Orientation::LandscapeCounterClockwise
                              : r == FrameRotation::PortraitInverted ? Orientation::PortraitInverted
                              : r == FrameRotation::UpsideDown       ? Orientation::LandscapeClockwise
                                                                     : Orientation::Portrait);
    }
    ~TurnedUi() { uikit::setOrientation(was); }
};
}  // namespace

// A page that could not be drawn says so (it was a blank page with only the status bar).
static void drawPageProblem(const char* why) {
    using namespace uikit;
    Rect area = insetX(screen(), 30);
    takeTop(area, (int16_t)(area.height / 3));
    const char* title = tr(MsgId::READER_PAGE_ERR_TITLE);
    text(takeTop(area, (int16_t)(wrappedHeight(title, FontTitle, area.width, 2) + 12)), title, FontTitle, Color::Black,
         uikit::TextAlign::Center, 2);
    text(takeTop(area, wrappedHeight(why, FontBody, area.width, 3)), why, FontBody, Color::Black,
         uikit::TextAlign::Center, 3);
}

void BookReader::redrawForStatusBar() {
    _statusBarRedraw = true;
    renderCurrentPage();
    _statusBarRedraw = false;
}

void BookReader::renderCurrentPage() {
    if (!_opened) return;
    TurnedUi turned(frameRotation());
    const uint32_t t0 = millis();
    if (!resolve()) {
        Serial.printf("[Reader] Chapter %d cannot be shown (layout failed or no memory)\n", _spine);
        Display.clear(EPD_WHITE);
        drawStatusBar();
        drawPageProblem(tr(_noMemory || MemStats::now().largestBlock < kLayoutBytes ? MsgId::READER_PAGE_ERR_MEMORY
                                                                                    : MsgId::READER_PAGE_ERR_BROKEN));
        Display.update(REFRESH_PARTIAL);
        return;
    }
    const uint32_t tFound = millis();
    Serial.printf("[Reader] Page %d/%d (chapter %d, page %d/%u)\n", getCurrentPage(), getTotalPages(), _spine,
                  _page + 1, (unsigned)curCount());
    _ttf.reserve();   // after a sync gave it back: before the page record and a layout job
    Display.clear(EPD_WHITE);
    drawStatusBar();
    const uint32_t tBar = millis();
    uint32_t tRead = 0, tText = 0;
    unsigned imageCount = 0;
    // A page record: runs plus text; justified text is many runs. Retried larger
    // when the page does not fit (dense pages need ~15 KB).
    for (size_t cap = kRenderBytes; cap <= Platform::kMemory.pageRecordMax; cap *= 2) {
        if (_bg && (g_liveTransient + cap > kTransientBudget || MemStats::now().largestBlock < cap + 4096)) {
            Serial.printf(
                    "[Reader] Page record of %u B does not fit beside the layout job (held %u B, largest block %u B)\n",
                    (unsigned)cap, (unsigned)g_liveTransient, (unsigned)MemStats::now().largestBlock);
            if (building())
                finishBackground();   // the page on screen comes from it: finished, not dropped
            else
                stopBackground();
        }
        Buffer mem(cap);
        if (!mem.p) {
            Serial.printf("[Reader] No memory to draw the page (%u B)\n", (unsigned)cap);
            _renderFailures++;
            drawPageProblem(tr(MsgId::READER_PAGE_ERR_MEMORY));
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
            drawPageProblem(
                    tr(st == BookStatus::OutOfMemory ? MsgId::READER_PAGE_ERR_MEMORY : MsgId::READER_PAGE_ERR_BROKEN));
            break;
        }
        FrameTarget frame{Display.getBuffer(),      (int16_t)EPD_WIDTH,      (int16_t)EPD_HEIGHT,
                          (int16_t)(EPD_WIDTH / 8), FrameFormat::Mono1Sharp, frameRotation()};
        uint32_t firstMissing = 0;
        tRead = millis();
        const uint32_t missing = PageRenderer::renderText(page, _fonts, frame, &firstMissing);
        // The page's first words, for a bookmark made here.
        _snippet = "";
        for (uint16_t r = 0; r < page.runCount && _snippet.length() < 90; r++) {
            if (_snippet.length() && _snippet[_snippet.length() - 1] != ' ') _snippet += ' ';
            const char* t = page.runs[r].text;
            for (uint16_t i = 0; i < page.runs[r].len; i++) {
                // Stop at 120 bytes, on a character start.
                if (_snippet.length() >= 120 && (static_cast<uint8_t>(t[i]) & 0xC0) != 0x80) break;
                _snippet += t[i];
            }
        }
        tText = millis();
        imageCount = page.imageCount;
        if (missing)
            Serial.printf("[Reader] %u characters not in the font (first U+%04X)\n", (unsigned)missing,
                          (unsigned)firstMissing);
        if (page.imageCount > 0) {
            auto drawImages = [&](size_t size) {
                Buffer img(size);
                if (!img.p) return BookStatus::OutOfMemory;
                Arena a(img.p, size);
                return drawImagesInto(page, _src, _catalog.zip(), a, frame, _sharpImages);
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
    // A full refresh every _fullEvery pages, counted from the first one: opening a
    // book from the (light) menus no longer costs a 3.8 s full refresh.
    // A redraw for the status bar (the clock) is not a page: not counted.
    if (!_statusBarRedraw) _renderedPages++;
    const bool full = _forceFull || (!_statusBarRedraw && _fullEvery > 0 && _renderedPages % _fullEvery == 0);
    _forceFull = false;
    const uint32_t tEnd = millis();
    // Where the time goes: finding the page (layout, cache), its record, the text, the images.
    const TtfFamily::Stats fs = _ttf.takeStats();
    Serial.printf(
            "[Reader] Page drawn in %lu ms (find %lu, status bar %lu, record %lu, text %lu, %u images %lu); "
            "glyphs %u rasterized (%u us each), %u cached, %u cache flushes, %u short of memory, font caches %u B\n",
            (unsigned long)(tEnd - t0), (unsigned long)(tFound - t0), (unsigned long)(tBar - tFound),
            (unsigned long)(tRead ? tRead - tBar : 0), (unsigned long)(tText ? tText - tRead : 0), (unsigned)imageCount,
            (unsigned long)(tText ? tEnd - tText : 0), (unsigned)fs.rasterized,
            (unsigned)(fs.rasterized ? fs.rasterUs / fs.rasterized : 0), (unsigned)fs.glyphHits, (unsigned)fs.flushes,
            (unsigned)fs.noMemory, (unsigned)_ttf.cacheBytes());
    Display.update(full ? REFRESH_FULL : REFRESH_PARTIAL);
}
