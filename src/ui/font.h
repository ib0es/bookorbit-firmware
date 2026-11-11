#pragma once

#include <Arduino.h>
#include "../platform/hal/display.h"

namespace bookfonts { struct Font; }

// Where book text goes on the page, in logical (rotated) pixels. Shared by the
// paginator and the page renderer so both agree; derived from the screen size,
// and equal to the historic 20/42/440/745 on a 480x800 portrait screen.
struct BookPageGeometry {
    int left;
    int top;
    int width;
    int maxY;   // last line may start at or above this y
};
BookPageGeometry bookPageGeometry();

class FontEngine {
public:
    FontEngine();

    void begin(Adafruit_GFX& gfx);

    // Book fonts (src/ui/book_fonts.h): `scale` is the size index,
    // 0 = 12 px ... kSizeCount-1 = 48 px, in steps of 2.
    static constexpr int kSizeCount = 19;
    static constexpr int kDefaultSize = 2;          // 16 px
    static int sizePx(int scale);                   // 12..48
    static int headingSize(int body);               // chapter titles: 6 px larger (max 48)
    static int paragraphIndent(int body);           // first-line indent, px
    static int clampSize(int scale);

    // Raw access to the book fonts for the layout engine's font adapter
    // (src/reader/engine/bitmap_book_font.cpp); book_fonts.h is compiled here only.
    struct GlyphInfo {
        const uint8_t* bits;   // 1 bpp, MSB first, rows not padded
        uint8_t width, height;
        int8_t xOffset, yOffset;
        uint8_t advance;
    };
    static int fontIndexForPx(int px);                  // nearest size
    static int fontPx(int index);
    static int fontAscent(int index);
    static int fontDescent(int index);
    static bool glyph(int index, uint32_t cp, GlyphInfo& out);   // false: not in the font

    // Draw string with word wrapping
    // Wraps within `maxWidth` px from x (-1: remaining display width); stops before
    // a line would start below maxY (-1: bottom less 40 px). Returns the y under the last line.
    int drawString(int x, int y, const char* text, EPDColor color = EPD_BLACK, int scale = 1, int maxWidth = -1,
                   int maxY = -1);

    // Book page typography: centered headings, bold via overstrike, paragraph indents, and \f break
    int drawBookPage(int x, int y, const char* text, int maxWidth = 440, bool startsWithParagraph = true, int fontScale = 1);

    // Calculate pixel width of a string (UTF-8 supported)
    int getTextWidth(const char* text, int scale = 1);

    // Font metrics
    int getFontHeight(int scale = 1);
    int getFontAscent(int scale = 1);
    void selectFont(int scale);

private:
    Adafruit_GFX* _gfx = nullptr;
    bool _initialized;
    int _currentScale;
    const bookfonts::Font* _font = nullptr;
    uint16_t _color = 0;   // GFX color of the text (0 = black)

    int ascentPx() const;
    int descentPx() const;   // below the baseline, positive
    int measure(const char* text) const;
    void drawUtf8(int x, int baseline, const char* text) const;
};

extern FontEngine Font;
