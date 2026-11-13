#include "book_reader_internal.h"
#include "../ui/kit/ui_kit.h"   // the page-problem notice, turning the UI target (KNOWN_EXCEPTIONS)

namespace {
// The interface's own turn (menus drawn on top of a book's pages follow it).
freeink::book::FrameRotation uiFrameRotation() {
    using freeink::ui::Orientation;
    using freeink::book::FrameRotation;
    switch (uikit::target().orientation()) {
        case Orientation::LandscapeCounterClockwise: return FrameRotation::None;
        case Orientation::PortraitInverted: return FrameRotation::PortraitInverted;
        case Orientation::LandscapeClockwise: return FrameRotation::UpsideDown;
        default: return FrameRotation::Portrait;
    }
}
}  // namespace

int BookReader::drawSample(int x, int y, int width, int height, const char* utf8, uint16_t sizePx,
                           uint16_t lineSpacingPct) {
    uint8_t* fb = Display.getBuffer();
    if (!fb) return 0;
    freeink::book::FrameTarget frame{fb, (int16_t)EPD_WIDTH, (int16_t)EPD_HEIGHT, (int16_t)(EPD_WIDTH / 8),
                                     freeink::book::FrameFormat::Mono1Sharp, uiFrameRotation()};
    const int lh = _fonts.lineHeight(sizePx) * lineSpacingPct / 100, asc = _fonts.ascent(sizePx);
    int top = y;
    const char* p = utf8;
    while (*p && top + lh <= y + height) {
        // One line: as many whole words as fit (a word longer than the line is cut).
        std::string line;
        int w = 0, lastSpace = -1;
        const char* lineEnd = p;
        const char* q = p;
        uint32_t prev = 0;
        while (*q) {
            const char* start = q;
            uint32_t cp = (uint8_t)*q++;
            if (cp >= 0xC0) {
                const int extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : 1;
                cp &= 0x3F >> extra;
                for (int i = 0; i < extra && (*q & 0xC0) == 0x80; i++) cp = (cp << 6) | (*q++ & 0x3F);
            }
            w += (prev ? _fonts.kerning(prev, cp, sizePx, 0) : 0) + _fonts.advance(cp, sizePx, 0);
            if (w > width && !line.empty()) {
                if (lastSpace > 0) {
                    line.resize((size_t)lastSpace);
                    lineEnd = p + lastSpace + 1;
                } else {
                    lineEnd = start;
                }
                break;
            }
            if (cp == ' ') lastSpace = (int)line.size();
            line.append(start, (size_t)(q - start));
            lineEnd = q;
            prev = cp;
        }
        freeink::book::PageTextRun run{};
        run.x = (int16_t)x;
        run.baselineY = (int16_t)(top + asc);
        run.sizePx = sizePx;
        run.text = line.c_str();
        run.len = (uint16_t)line.size();
        freeink::book::Page page{};
        page.runs = &run;
        page.runCount = 1;
        PageRenderer::renderText(page, _fonts, frame);
        top += lh;
        p = lineEnd;
        while (*p == ' ') p++;
    }
    return top - y;
}

void BookReader::drawPreview(int y, int height, const char* text, const LayoutOptions& o, int sizeIndex) {
    using namespace freeink::book;
    LayoutParams p;
    p.pageWidth = (int16_t)Display.width();
    p.pageHeight = (int16_t)height;
    p.marginLeft = p.marginRight = (int16_t)o.marginPx;
    p.marginTop = p.marginBottom = 0;
    p.baseSizePx = (uint16_t)FontEngine::sizePx(sizeIndex);
    p.font = &_fonts;
    p.defaultAlign = o.justify ? TextAlign::Justify : TextAlign::Left;
    // The sample's own language (the menu's), for its hyphenation.
    bool cyrillic = false;
    for (const char* c = text; *c && !cyrillic; c++) cyrillic = (uint8_t)*c == 0xD0 || (uint8_t)*c == 0xD1;
    p.language = cyrillic ? "ru" : "en";
    p.hyphenator = o.hyphenate ? hyphenatorFor(p.language) : nullptr;
    p.stylesheet = o.bookStyles && _opened ? _catalog.stylesheet() : nullptr;
    p.embeddedStyles = o.bookStyles;
    p.lineSpacingPct = o.lineSpacingPct;
    p.paragraphSpacingPct = o.paragraphSpacingPct;
    p.orphanLines = p.widowLines = o.keepLines ? 2 : 1;
    p.focusReading = o.focusReading;

    struct TextSource : BookSource {
        const char* s;
        uint32_t n;
        int32_t readAt(uint64_t off, void* dst, uint32_t len) override {
            if (off >= n) return 0;
            const uint32_t k = std::min<uint32_t>(len, n - (uint32_t)off);
            memcpy(dst, s + off, k);
            return (int32_t)k;
        }
        uint64_t size() const override { return n; }
    } src;
    src.s = text;
    src.n = (uint32_t)strlen(text);
    // The first page, drawn moved down to the box.
    struct Draw : PageSink {
        FontChain* fonts = nullptr;
        FrameRotation rotation = FrameRotation::Portrait;
        int dy = 0;
        int clipTop = 0, ascent = 0;
        bool drawn = false;
        bool onPage(const Page& page) override {
            if (drawn) return true;   // the first page only (see Lines)
            uint8_t* fb = Display.getBuffer();
            FrameTarget frame{fb, (int16_t)EPD_WIDTH, (int16_t)EPD_HEIGHT, (int16_t)(EPD_WIDTH / 8),
                              FrameFormat::Mono1Sharp, rotation};
            PageTextRun runs[48];
            uint16_t n = 0;
            for (uint16_t i = 0; i < page.runCount; i++) {
                if (n == 48) {   // in batches: a justified line is a run per word
                    Page part{};
                    part.runs = runs;
                    part.runCount = n;
                    PageRenderer::renderText(part, *fonts, frame);
                    n = 0;
                }
                if (page.runs[i].baselineY + dy - ascent < clipTop) continue;   // above the box
                runs[n] = page.runs[i];
                runs[n].baselineY = (int16_t)(runs[n].baselineY + dy);
                n++;
            }
            Page part{};
            part.runs = runs;
            part.runCount = n;
            PageRenderer::renderText(part, *fonts, frame);
            drawn = true;
            return true;
        }
    } sink;
    sink.fonts = &_fonts;
    sink.rotation = uiFrameRotation();
    sink.dy = y;
    sink.clipTop = y;
    sink.ascent = _fonts.ascent(p.baseSizePx);
    BookStatus st = BookStatus::OutOfMemory;
    // The engine's layout state: ~36 KB for this page on the host, ~41 KB on the
    // X4 (with a book's stylesheet): a bigger arena when it runs out.
    stopBackground();
    struct Lines : PageSink {
        const char* firstWord = nullptr;   // of the last paragraph
        size_t wordLen = 0;
        int base[64] = {};                 // the first page's line baselines
        int count = 0, found = -1;         // the second paragraph's first line
        bool pageSeen = false;
        bool onPage(const Page& page) override {
            // The first page only; the rest are let through (stopping the engine
            // early makes it report running out of memory).
            if (pageSeen) return true;
            pageSeen = true;
            for (uint16_t i = 0; i < page.runCount && count < 64; i++) {
                const PageTextRun& r = page.runs[i];
                if (count > 0 && r.baselineY == base[count - 1]) continue;   // the same line
                if (found < 0 && r.len >= wordLen && memcmp(r.text, firstWord, wordLen) == 0) found = count;
                base[count++] = r.baselineY;
            }
            return true;
        }
    };
    // The last paragraph is the one cut at the page's foot: the gaps between the
    // others stay in view (the paragraph spacing setting).
    const char* second = nullptr;
    for (const char* q = strstr(text, "\n\n"); q; q = strstr(q + 2, "\n\n")) second = q;
    // The cut of the page (the first pass) depends only on these: kept from the
    // last drawing, so moving through the menu or a change that keeps it lays the
    // sample out once instead of twice. The arena that was big enough is tried
    // first (40 KB never was with a book's stylesheet: a third pass every time).
    struct Cut {
        LayoutOptions o;
        int size = -1, width = -1, height = -1;
        const char* text = nullptr;
        uint32_t family = 0;   // a hash of the name: no String kept for good
        bool styles = false;
        int16_t pageHeight = 0;
    };
    static Cut s_cut;
    uint32_t familyKey = 2166136261u;
    for (size_t i = 0; i < _family.length(); i++) familyKey = (familyKey ^ (uint8_t)_family[i]) * 16777619u;
    static size_t s_cap = 40 * 1024;
    const bool cutKnown = s_cut.o == o && s_cut.size == sizeIndex && s_cut.width == p.pageWidth &&
                          s_cut.height == height && s_cut.text == text &&
                          s_cut.family == familyKey && s_cut.styles == (p.stylesheet != nullptr);
    const size_t kCaps[] = {(size_t)40 * 1024, (size_t)52 * 1024, (size_t)64 * 1024};
    for (size_t cap : kCaps) {
        if (cap < s_cap) continue;
        Buffer mem(cap);
        if (!mem.p) {
            Serial.printf("[Reader] Appearance preview: no %u B block (largest %u B)\n", (unsigned)cap,
                          (unsigned)MemStats::now().largestBlock);
            continue;
        }
        // The page is cut right under the first line of the second paragraph (as laid
        // out allowing lone lines): "no lone lines" then moves that line on, as it
        // would at the foot of a page, so the setting shows. Found by a first pass.
        LayoutParams q = p;
        sink.dy = y;
        if (cutKnown) {
            q.pageHeight = s_cut.pageHeight;
        } else if (second) {
            Lines lines;
            lines.firstWord = second + 2;
            lines.wordLen = strcspn(second + 2, " ");
            LayoutParams loose = p;
            loose.pageHeight = 1600;   // the whole sample on one page (big sizes too)
            loose.orphanLines = loose.widowLines = 1;
            Arena a(mem.p, cap);
            const BookStatus s1 = ChapterLayout::layoutPlainText(src, loose, a, lines);
            if (s1 == BookStatus::OutOfMemory) {
                Serial.printf("[Reader] Appearance preview: %u B is not enough (%u used)\n", (unsigned)cap, (unsigned)a.highWater());
                continue;
            }
            // A page that ends just under line k: line k+1 no longer fits.
            const int asc = _fonts.ascent(p.baseSizePx);
            auto endsAfter = [&](int k) { return lines.base[k + 1] - asc; };
            if (lines.found > 0 && lines.found + 1 < lines.count && endsAfter(lines.found) < height) {
                // the last paragraph's first line alone at the foot (an orphan)
                q.pageHeight = (int16_t)endsAfter(lines.found);
            } else if (lines.found > 1 && endsAfter(lines.found - 2) < height) {
                // too big a size for that: the paragraph before's last line alone on
                // the next page (a widow), which "no lone lines" takes one more with.
                // (Bigger still, the box ends inside the first paragraph: the sample
                // is always shown from its start, and the setting cannot show.)
                q.pageHeight = (int16_t)endsAfter(lines.found - 2);
            }
        }
        Arena a(mem.p, cap);
        st = ChapterLayout::layoutPlainText(src, q, a, sink);
        if (sink.drawn) {
            Serial.printf("[Reader] Appearance preview laid out: %u of %u B%s\n", (unsigned)a.highWater(), (unsigned)cap,
                          cutKnown ? " (cut kept)" : "");
            s_cap = cap;
            s_cut.o = o;
            s_cut.size = sizeIndex;
            s_cut.width = p.pageWidth;
            s_cut.height = height;
            s_cut.text = text;
            s_cut.family = familyKey;
            s_cut.styles = p.stylesheet != nullptr;
            s_cut.pageHeight = q.pageHeight;
            break;
        }
        Serial.printf("[Reader] Appearance preview: %s with %u B\n", bookStatusName(st), (unsigned)cap);
    }
    if (!sink.drawn) {
        Serial.printf("[Reader] Appearance preview not laid out (%s): a plain sample instead\n", bookStatusName(st));
        drawSample(o.marginPx, y, Display.width() - 2 * o.marginPx, height, text, (uint16_t)FontEngine::sizePx(sizeIndex),
                   o.lineSpacingPct);
    }
}

const char* BookReader::getFontSizeName(int size) {
    static char buf[8][12];   // a few names may be alive at once (menus)
    static int next = 0;
    char* out = buf[next++ & 7];
    snprintf(out, sizeof(buf[0]), "%d pt", FontEngine::sizePx(size));
    return out;
}

