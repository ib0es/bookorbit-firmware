#include "font.h"
#include "book_fonts.h"

FontEngine Font;

static_assert(FontEngine::kSizeCount == bookfonts::kFontCount, "regenerate src/ui/book_fonts.h");

FontEngine::FontEngine() : _initialized(false), _currentScale(-1) {}

int FontEngine::clampSize(int scale) {
    return scale < 0 ? 0 : (scale >= kSizeCount ? kSizeCount - 1 : scale);
}
int FontEngine::sizePx(int scale) { return 12 + 2 * clampSize(scale); }
int FontEngine::headingSize(int body) { return clampSize(clampSize(body) + 3); }
int FontEngine::paragraphIndent(int body) { return (sizePx(body) * 3 + 1) / 2; }

void FontEngine::begin(Adafruit_GFX& gfx) {
    _gfx = &gfx;
    _initialized = true;
    _currentScale = -1;
}

void FontEngine::selectFont(int scale) {
    scale = clampSize(scale);
    if (scale == _currentScale && _font) return;
    _currentScale = scale;
    _font = &bookfonts::kFonts[scale];
}


int FontEngine::ascentPx() const { return (*_font).ascent; }
int FontEngine::descentPx() const { return (*_font).descent; }

// Next code point of UTF-8 text (0 at the end); invalid bytes are skipped.
static uint32_t nextCodepoint(const uint8_t*& p) {
    while (*p) {
        const uint8_t c = p[0];
        if (c < 0x80) { p += 1; return c; }
        if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { const uint32_t cp = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return cp; }
        if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            const uint32_t cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return cp;
        }
        if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            p += 4; return '?';   // outside the BMP: not in the fonts
        }
        p += 1;   // stray byte
    }
    return 0;
}

static const bookfonts::Glyph* findGlyph(const bookfonts::Font& f, uint32_t cp) {
    int lo = 0, hi = (int)f.count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const uint16_t c = f.codepoints[mid];
        if (c == cp) return &f.glyphs[mid];
        if (c < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

static const bookfonts::Glyph* glyphOrFallback(const bookfonts::Font& f, uint32_t cp) {
    if (cp == 0x00A0) cp = ' ';
    const bookfonts::Glyph* g = findGlyph(f, cp);
    return g ? g : findGlyph(f, '?');
}

int FontEngine::measure(const char* text) const {
    const bookfonts::Font& f = (*_font);
    const uint8_t* p = (const uint8_t*)text;
    int w = 0;
    for (uint32_t cp; (cp = nextCodepoint(p)) != 0;) {
        if (const bookfonts::Glyph* g = glyphOrFallback(f, cp)) w += g->advance;
    }
    return w;
}

void FontEngine::drawUtf8(int x, int baseline, const char* text) const {
    if (!_gfx) return;
    const bookfonts::Font& f = (*_font);
    const uint8_t* p = (const uint8_t*)text;
    for (uint32_t cp; (cp = nextCodepoint(p)) != 0;) {
        const bookfonts::Glyph* g = glyphOrFallback(f, cp);
        if (!g) continue;
        const uint8_t* bits = f.bitmaps + g->offset;
        const int x0 = x + g->xOffset, y0 = baseline + g->yOffset;
        uint32_t bit = 0;
        for (int row = 0; row < g->height; row++) {
            for (int col = 0; col < g->width; col++, bit++) {
                if (bits[bit >> 3] & (0x80 >> (bit & 7))) _gfx->drawPixel(x0 + col, y0 + row, _color);
            }
        }
        x += g->advance;
    }
}

int FontEngine::getFontAscent(int scale) {
    selectFont(scale);
    return ascentPx();
}

int FontEngine::getFontHeight(int scale) {
    selectFont(scale);
    return ascentPx() + descentPx() + 4;
}

int FontEngine::getTextWidth(const char* text, int scale) {
    if (!text || !*text) return 0;
    if (!_initialized) begin(Display);
    selectFont(scale);
    return measure(text);
}

int FontEngine::drawString(int x, int y, const char* text, EPDColor color, int scale, int maxWidth, int maxY) {
    if (!text || !*text) return y;
    if (!_initialized) begin(Display);

    selectFont(scale);
        _color = (color == EPD_BLACK) ? 0 : 1;

    int ascent = ascentPx();
    int descent = -descentPx(); // below the baseline, as a negative number
    int lineHeight = ascent - descent + 4;

    int curX = x;
    int curY = y;
    int spaceWidth = measure(" ");

    const char* p = text;
    while (*p) {
        if (*p == '\r') {
            p++;
            continue;
        }

        // Explicit newline
        if (*p == '\n') {
            p++;
            curX = x;
            curY += lineHeight;
            continue;
        }

        // Skip leading space at beginning of line
        if (curX == x && *p == ' ') {
            p++;
            continue;
        }

        // Inter-word space
        if (*p == ' ') {
            curX += spaceWidth;
            p++;
            continue;
        }

        // Find next word boundary (up to 250 bytes per chunk)
        const char* wordStart = p;
        while (*p && *p != ' ' && *p != '\n' && (p - wordStart < 250)) {
            p++;
        }
        int wordLen = p - wordStart;

        char wordBuf[256];
        int copyLen = (wordLen < (int)sizeof(wordBuf) - 1) ? wordLen : (sizeof(wordBuf) - 1);
        while (copyLen > 0 && (wordStart[copyLen] & 0xC0) == 0x80) {
            copyLen--;
        }
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        int wordWidth = measure(wordBuf);

        // Word-wrap check
        if (curX + wordWidth > x + maxWidth && curX > x) {
            curX = x;
            curY += lineHeight;
        }

        // Clip to current display height (supports both portrait 800 and landscape 480)
        if (curY + lineHeight > (maxY >= 0 ? maxY : Display.height() - 40)) break;

        // Draw word at baseline
        drawUtf8(curX, curY + ascent, wordBuf);
        curX += wordWidth;
    }

    return curY + lineHeight;
}

BookPageGeometry bookPageGeometry() {
    return BookPageGeometry{20, 42, Display.width() - 40, Display.height() - 55};
}

int FontEngine::drawBookPage(int x, int y, const char* text, int maxWidth, bool startsWithParagraph, int fontScale) {
    if (!text || !*text) return y;
    if (!_initialized) begin(Display);

        _color = 0;  // black

    int bodyScale = clampSize(fontScale);
    int headerScale = headingSize(bodyScale);

    selectFont(bodyScale);
    int ascent1 = ascentPx();
    int descent1 = (-descentPx());
    int lineHeight1 = ascent1 - descent1 + 4;
    int spaceWidth1 = measure(" ");

    selectFont(headerScale);
    int ascent2 = ascentPx();
    int descent2 = (-descentPx());
    int lineHeight2 = ascent2 - descent2 + 4;

    selectFont(bodyScale);

    const int PARAGRAPH_INDENT = paragraphIndent(bodyScale);
    const int MAX_Y = bookPageGeometry().maxY; // leave room above the bottom bar

    int curX = x;
    int curY = y;
    bool isNewParagraph = startsWithParagraph;
    bool isBold = false;

    const char* p = text;

    while (*p && curY <= MAX_Y) {
        if (*p == '\r') {
            p++;
            continue;
        }

        if (*p == '\f') {
            // Form feed: end of chapter / explicit page break
            break;
        }

        // Start of line checks (Headings & Indentation)
        if (curX == x) {
            // Skip leading spaces at start of line
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;

            // Check for chapter heading:
            // 1) Markdown header: "# ", "## ", "### "
            // 2) Typical chapter title line: "Глава ", "ГЛАВА ", "Часть ", "ЧАСТЬ ", etc.
            bool isHeading = false;
            int hashCount = 0;
            const char* hp = p;
            while (*hp == '#') {
                hashCount++;
                hp++;
            }
            if (hashCount > 0 && *hp == ' ') {
                isHeading = true;
                p = hp + 1;
            } else if (strncmp(p, "Глава ", 11) == 0 || strncmp(p, "ГЛАВА ", 11) == 0 ||
                       strncmp(p, "Часть ", 11) == 0 || strncmp(p, "ЧАСТЬ ", 11) == 0 ||
                       strncmp(p, "Пролог", 12) == 0 || strncmp(p, "ПРОЛОГ", 12) == 0 ||
                       strncmp(p, "Эпилог", 12) == 0 || strncmp(p, "ЭПИЛОГ", 12) == 0 ||
                       strncmp(p, "Chapter ", 8) == 0 || strncmp(p, "CHAPTER ", 8) == 0) {
                const char* eol = strchr(p, '\n');
                if (eol && (eol - p < 70)) {
                    isHeading = true;
                }
            }

            if (isHeading) {
                const char* hStart = p;
                while (*p && *p != '\n' && *p != '\r' && *p != '\f') p++;
                size_t hLen = p - hStart;

                char hBuf[128];
                size_t copyLen = (hLen < sizeof(hBuf) - 1) ? hLen : sizeof(hBuf) - 1;
                while (copyLen > 0 && (hStart[copyLen] & 0xC0) == 0x80) {
                    copyLen--;
                }
                memcpy(hBuf, hStart, copyLen);
                hBuf[copyLen] = '\0';

                // Clean markdown bold tokens inside heading text
                char cleanHBuf[128];
                size_t cIdx = 0;
                for (size_t i = 0; hBuf[i] && cIdx < sizeof(cleanHBuf) - 1; i++) {
                    if (hBuf[i] == '*' && hBuf[i + 1] == '*') {
                        i++;
                        continue;
                    }
                    cleanHBuf[cIdx++] = hBuf[i];
                }
                cleanHBuf[cIdx] = '\0';

                selectFont(headerScale);
                int hWidth = measure(cleanHBuf);
                int hX = (hWidth < maxWidth) ? (x + (maxWidth - hWidth) / 2) : x;

                if (curY + lineHeight2 <= MAX_Y) {
                    // Draw centered chapter title in large font
                    drawUtf8(hX, curY + ascent2, cleanHBuf);
                    drawUtf8(hX + 1, curY + ascent2, cleanHBuf); // Overstrike for crisp boldness
                    curY += lineHeight2 + 8;
                }

                selectFont(bodyScale);
                curX = x;
                isNewParagraph = true;

                while (*p == '\n' || *p == '\r') p++;
                continue;
            }

            // Normal line: apply paragraph indent if first line of paragraph
            if (isNewParagraph) {
                curX = x + PARAGRAPH_INDENT;
                isNewParagraph = false;
            }
        }

        // Check for Markdown bold toggle "**"
        if (p[0] == '*' && p[1] == '*') {
            isBold = !isBold;
            p += 2;
            continue;
        }

        // Newline handling
        if (*p == '\n') {
            p++;
            curX = x;
            curY += lineHeight1;

            if (*p == '\n' || *p == '\r') {
                while (*p == '\n' || *p == '\r') p++;
                curY += 8; // subtle paragraph gap
            }
            isNewParagraph = true;
            continue;
        }

        // Inter-word space
        if (*p == ' ') {
            curX += spaceWidth1;
            p++;
            continue;
        }

        // Read word
        const char* wordStart = p;
        while (*p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\f' && !(p[0] == '*' && p[1] == '*')) {
            p++;
        }
        size_t wordLen = p - wordStart;

        char wordBuf[256];
        size_t copyLen = (wordLen < sizeof(wordBuf) - 1) ? wordLen : sizeof(wordBuf) - 1;
        while (copyLen > 0 && (wordStart[copyLen] & 0xC0) == 0x80) {
            copyLen--;
        }
        memcpy(wordBuf, wordStart, copyLen);
        wordBuf[copyLen] = '\0';

        int wordWidth = measure(wordBuf);
        int extraBoldW = isBold ? 1 : 0;

        // Word-wrap check
        if (curX + wordWidth + extraBoldW > x + maxWidth && curX > x) {
            curX = x;
            curY += lineHeight1;
        }

        if (curY > MAX_Y) break;

        // Draw word at baseline
        drawUtf8(curX, curY + ascent1, wordBuf);
        if (isBold) {
            drawUtf8(curX + 1, curY + ascent1, wordBuf);
        }

        curX += wordWidth + extraBoldW;
    }

    return curY;
}
