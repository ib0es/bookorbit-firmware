#pragma once

// TrueType book fonts for FreeInkBook, made for a small heap (ESP32-C3, no
// PSRAM): the font files are read in place (flash), and every face of a
// family shares one set of caches, all bounded and all droppable (a sync needs
// the memory back; they fill again as pages are drawn):
//  - advances and glyph ids, by code point (the layout's hot path);
//  - kerning pairs ('kern' or GPOS lookups are slow in flash);
//  - rasterized glyphs, 1 bit per pixel (the pages are drawn 1-bit), about an
//    eighth of what 8-bit coverage takes: a page's ~100 distinct glyphs fit.
// A glyph comes back as 8-bit coverage (0/255) from a scratch buffer that
// holds until the next rasterize(), as RenderFont allows.

#include <BookFont.h>
#include <stddef.h>
#include <stdint.h>

struct stbtt_fontinfo;

class TtfFamily;

// One face (regular, bold, italic or bold italic) of a family.
class TtfFace : public freeink::book::RenderFont {
public:
    bool hasGlyph(uint32_t codepoint) const override;
    int16_t advance(uint32_t codepoint, uint16_t sizePx, uint8_t styleFlags) override;
    int16_t lineHeight(uint16_t sizePx) override;
    int16_t ascent(uint16_t sizePx) override;
    int16_t kerning(uint32_t left, uint32_t right, uint16_t sizePx, uint8_t styleFlags) override;
    uint32_t ligature(uint32_t left, uint32_t right, uint8_t styleFlags) override;
    const freeink::book::GlyphBitmap* rasterize(uint32_t codepoint, uint16_t sizePx) override;

private:
    friend class TtfFamily;
    float scale(uint16_t sizePx) const;
    int16_t px(int32_t units, uint16_t sizePx) const;
    int glyphOf(uint32_t codepoint, int16_t* unscaledAdvance) const;

    TtfFamily* _family = nullptr;
    stbtt_fontinfo* _info = nullptr;
    uint8_t _id = 0;
    bool _kerns = false;
    bool _ligFi = false, _ligFl = false;
    float _unitsPerEmInv = 0;
    int32_t _unitsPerEm = 1000;
    int _ascent = 0, _descent = 0, _lineGap = 0;   // font units
};

// A family: up to four faces over font files that stay where they are, and
// the caches they share.
class TtfFamily {
public:
    TtfFamily() = default;
    ~TtfFamily();
    TtfFamily(const TtfFamily&) = delete;
    TtfFamily& operator=(const TtfFamily&) = delete;

    // Faces by style (StyleBold | StyleItalic); a missing one is null.
    bool addFace(uint8_t style, const uint8_t* data, uint32_t len);
    TtfFace* face(uint8_t style) { return _faces[style & 3].fontReady() ? &_faces[style & 3].face : nullptr; }

    // The caches: allocated on first use (or by reserve(), early, before big
    // buffers come and go around them), given back by release(), filled again later.
    bool reserve();
    void release();
    size_t cacheBytes() const;

    // Cache statistics since the last call, for the page-draw log.
    struct Stats { uint32_t rasterized = 0, glyphHits = 0, flushes = 0, noMemory = 0, rasterUs = 0; };

    // Called when a glyph cannot be rasterized for lack of memory, to make room
    // (the glyph is then tried once more).
    void setNoMemoryHandler(void (*fn)(void*), void* user) { _noMemory = fn; _noMemoryUser = user; }
    Stats takeStats();

    // Identifies the fonts in layout cache names.
    uint32_t fingerprint() const { return _fingerprint; }

private:
    friend class TtfFace;
    struct Slot {
        TtfFace face;
        alignas(8) uint8_t info[160];
        bool ready = false;
        bool fontReady() const { return ready; }
    };
    struct AdvanceEntry {
        uint32_t key;      // face << 24 | code point, ~0 = empty
        int16_t advance;   // font units
        uint16_t glyph;
    };
    struct KernEntry {
        uint32_t key;      // face << 30 | glyph << 15 | glyph, ~0 = empty
        int16_t kern;      // font units
    };
    struct GlyphEntry {
        uint32_t key;      // face << 28 | size << 21 | code point, ~0 = empty
        uint16_t offset;   // into the bit store
        uint8_t width, height;
        int8_t xoff, yoff;
        uint8_t generation;
    };

    bool ensureCaches();
    uint8_t* scratch(size_t bytes);

    Slot _faces[4];
    uint32_t _fingerprint = 0x7F0E0000u;

    // Caches (one allocation).
    uint8_t* _mem = nullptr;
    AdvanceEntry* _advances = nullptr;
    KernEntry* _kerns = nullptr;
    GlyphEntry* _glyphs = nullptr;
    uint8_t* _bits = nullptr;
    uint32_t _bitsUsed = 0;
    uint8_t _generation = 1;   // glyph entries of another generation are empty

    uint8_t* _scratch = nullptr;   // 8-bit coverage of the glyph returned last
    size_t _scratchCap = 0;
    freeink::book::GlyphBitmap _glyph{};
    Stats _stats;
    void (*_noMemory)(void*) = nullptr;
    void* _noMemoryUser = nullptr;
};
