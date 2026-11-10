#include "bitmap_book_font.h"

#include "../../ui/font.h"

namespace {

bool lookup(uint32_t cp, uint16_t sizePx, FontEngine::GlyphInfo& g) {
    const int idx = FontEngine::fontIndexForPx(sizePx);
    if (FontEngine::glyph(idx, cp, g)) return true;
    const uint32_t alt = BitmapBookFont::normalized(cp);
    return alt != cp && FontEngine::glyph(idx, alt, g);
}

}  // namespace

uint32_t BitmapBookFont::normalized(uint32_t cp) {
    switch (cp) {
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0x02BC: return 0x27;   // '
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: return 0x22;                // "
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return 0x2D;
        case 0x2026: return 0x2E;                                                       // . of ...
        case 0x00A0: case 0x2002: case 0x2003: case 0x2009: case 0x200A: case 0x202F: return 0x20;
        case 0x2022: case 0x2219: return 0xB7;                                          // bullet -> middle dot
        default: return cp;
    }
}

bool BitmapBookFont::hasGlyph(uint32_t codepoint) const {
    FontEngine::GlyphInfo g;
    return lookup(codepoint, 16, g);
}

int16_t BitmapBookFont::advance(uint32_t codepoint, uint16_t sizePx, uint8_t) {
    FontEngine::GlyphInfo g;
    if (codepoint == 0x00AD || codepoint == 0x200B || codepoint == 0xFEFF) return 0;   // invisible
    if (lookup(codepoint, sizePx, g)) return g.advance;
    if (lookup(' ', sizePx, g)) return g.advance;   // a gap for the truly missing
    return (int16_t)(sizePx / 2);
}

int16_t BitmapBookFont::lineHeight(uint16_t sizePx) {
    const int idx = FontEngine::fontIndexForPx(sizePx);
    // Same leading as the firmware's own text (ascent + descent + 4).
    return (int16_t)(FontEngine::fontAscent(idx) + FontEngine::fontDescent(idx) + 4);
}

int16_t BitmapBookFont::ascent(uint16_t sizePx) {
    return (int16_t)FontEngine::fontAscent(FontEngine::fontIndexForPx(sizePx));
}

const freeink::book::GlyphBitmap* BitmapBookFont::rasterize(uint32_t codepoint, uint16_t sizePx) {
    FontEngine::GlyphInfo g;
    if (!lookup(codepoint, sizePx, g)) return nullptr;
    const uint32_t pixels = (uint32_t)g.width * g.height;
    if (pixels > sizeof(_coverage)) return nullptr;
    for (uint32_t i = 0; i < pixels; i++) _coverage[i] = (g.bits[i >> 3] & (0x80u >> (i & 7))) ? 255 : 0;
    _glyph.pixels = _coverage;
    _glyph.width = g.width;
    _glyph.height = g.height;
    _glyph.xoff = g.xOffset;
    _glyph.yoff = g.yOffset;
    _glyph.advance = g.advance;
    return &_glyph;
}
