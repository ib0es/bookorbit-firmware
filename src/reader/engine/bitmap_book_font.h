#pragma once

// The firmware's bitmap book font (DejaVu Serif Condensed, 12..48 px in steps
// of 2, Latin-1 + Cyrillic; src/ui/book_fonts.h) as a FreeInkBook RenderFont.
// Each requested size maps to the nearest baked one, so headings render larger
// than body text. Bold is synthesized by the page renderer (double strike);
// there is no italic face.

#include <BookFont.h>
#include <stdint.h>

class BitmapBookFont : public freeink::book::RenderFont {
public:
    bool hasGlyph(uint32_t codepoint) const override;
    int16_t advance(uint32_t codepoint, uint16_t sizePx, uint8_t styleFlags) override;
    int16_t lineHeight(uint16_t sizePx) override;
    int16_t ascent(uint16_t sizePx) override;
    const freeink::book::GlyphBitmap* rasterize(uint32_t codepoint, uint16_t sizePx) override;

    // Identifies the font set in layout cache names (bump when the fonts change).
    static constexpr uint32_t kFingerprint = 0xB0F0D501u;

    // Typographic punctuation the font lacks, as the ASCII it degrades to
    // ("’" -> "'", "—" -> "-"); the code point itself when nothing applies.
    static uint32_t normalized(uint32_t cp);

private:
    freeink::book::GlyphBitmap _glyph{};
    uint8_t _coverage[48 * 64];   // the largest glyph of the 48 px face fits
};
