#include "ttf_book_font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../../freeink-sdk/libs/font/FreeInkFont/third_party/stb/stb_truetype.h"   // not on the include path of src/
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using freeink::book::GlyphBitmap;
using freeink::book::StyleBold;
using freeink::book::StyleItalic;

// Cache sizes. A small heap (ESP32-C3) gets ~25 KB in all; more where there is PSRAM.
#ifndef TTF_ADVANCE_SLOTS
#define TTF_ADVANCE_SLOTS 512
#endif
#ifndef TTF_KERN_SLOTS
#define TTF_KERN_SLOTS 256
#endif
#ifndef TTF_GLYPH_SLOTS
#define TTF_GLYPH_SLOTS 384
#endif
#ifndef TTF_GLYPH_BITS_BYTES
#define TTF_GLYPH_BITS_BYTES (14 * 1024)
#endif

namespace {

constexpr uint32_t kEmpty = 0xFFFFFFFFu;
constexpr uint8_t kInk = 120;   // coverage that inks a pixel (PageRenderer's Mono1Sharp threshold)

inline uint32_t mix(uint32_t k) {
    k ^= k >> 16;
    k *= 0x7FEB352Du;
    k ^= k >> 15;
    return k;
}

}  // namespace

// ------------------------------------------------------------------ family

TtfFamily::~TtfFamily() {
    release();
    free(_scratch);
}

bool TtfFamily::addFace(uint8_t style, const uint8_t* data, uint32_t len) {
    static_assert(sizeof(stbtt_fontinfo) <= sizeof(Slot::info), "stbtt_fontinfo does not fit");
    Slot& s = _faces[style & 3];
    s.ready = false;
    if (!data || len < 12) return false;
    stbtt_fontinfo* info = reinterpret_cast<stbtt_fontinfo*>(s.info);
    const int offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(info, data, offset)) return false;
    TtfFace& f = s.face;
    f._family = this;
    f._info = info;
    f._id = style & 3;
    stbtt_GetFontVMetrics(info, &f._ascent, &f._descent, &f._lineGap);
    f._unitsPerEmInv = stbtt_ScaleForMappingEmToPixels(info, 1.0f);
    f._kerns = info->kern != 0 || info->gpos != 0;
    f._ligFi = stbtt_FindGlyphIndex(info, 0xFB01) != 0;
    f._ligFl = stbtt_FindGlyphIndex(info, 0xFB02) != 0;
    s.ready = true;
    // The fingerprint: sizes and a sample of each face's bytes.
    uint32_t h = _fingerprint ^ (len * 2654435761u) ^ (uint32_t)(style + 1);
    for (uint32_t i = 0; i < len; i += len / 64 + 1) h = mix(h ^ data[i]);
    _fingerprint = h;
    return true;
}

bool TtfFamily::ensureCaches() {
    if (_mem) return true;
    const size_t a = sizeof(AdvanceEntry) * TTF_ADVANCE_SLOTS, k = sizeof(KernEntry) * TTF_KERN_SLOTS,
                 g = sizeof(GlyphEntry) * TTF_GLYPH_SLOTS;
    _mem = (uint8_t*)malloc(a + k + g + TTF_GLYPH_BITS_BYTES);
    if (!_mem) return false;
    _advances = (AdvanceEntry*)_mem;
    _kerns = (KernEntry*)(_mem + a);
    _glyphs = (GlyphEntry*)(_mem + a + k);
    _bits = _mem + a + k + g;
    for (size_t i = 0; i < TTF_ADVANCE_SLOTS; i++) _advances[i].key = kEmpty;
    for (size_t i = 0; i < TTF_KERN_SLOTS; i++) _kerns[i].key = kEmpty;
    for (size_t i = 0; i < TTF_GLYPH_SLOTS; i++) _glyphs[i].key = kEmpty;
    _bitsUsed = 0;
    return true;
}

void TtfFamily::release() {
    free(_mem);
    _mem = nullptr;
    _advances = nullptr;
    _kerns = nullptr;
    _glyphs = nullptr;
    _bits = nullptr;
    _bitsUsed = 0;
    // The scratch holds one glyph only: a big heading glyph should not stay.
    free(_scratch);
    _scratch = nullptr;
    _scratchCap = 0;
}

size_t TtfFamily::cacheBytes() const {
    return (_mem ? sizeof(AdvanceEntry) * TTF_ADVANCE_SLOTS + sizeof(KernEntry) * TTF_KERN_SLOTS +
                       sizeof(GlyphEntry) * TTF_GLYPH_SLOTS + TTF_GLYPH_BITS_BYTES
                 : 0) +
           _scratchCap;
}

TtfFamily::Stats TtfFamily::takeStats() {
    const Stats s = _stats;
    _stats = Stats();
    return s;
}

uint8_t* TtfFamily::scratch(size_t bytes) {
    if (bytes <= _scratchCap) return _scratch;
    const size_t cap = (bytes + 1023) & ~(size_t)1023;
    uint8_t* p = (uint8_t*)realloc(_scratch, cap);
    if (!p) return nullptr;
    _scratch = p;
    _scratchCap = cap;
    return p;
}

// ------------------------------------------------------------------ face

float TtfFace::scale(uint16_t sizePx) const { return _unitsPerEmInv * (float)sizePx; }

int TtfFace::glyphOf(uint32_t cp, int16_t* unscaledAdvance) const {
    TtfFamily* fam = _family;
    const uint32_t key = ((uint32_t)_id << 24) | (cp & 0xFFFFFF);
    TtfFamily::AdvanceEntry* e = nullptr;
    if (fam->ensureCaches()) {
        e = &fam->_advances[mix(key) % TTF_ADVANCE_SLOTS];
        if (e->key == key) {
            if (unscaledAdvance) *unscaledAdvance = e->advance;
            return e->glyph;
        }
    }
    const int g = stbtt_FindGlyphIndex(_info, (int)cp);
    int adv = 0, lsb = 0;
    if (g) stbtt_GetGlyphHMetrics(_info, g, &adv, &lsb);
    if (e) {
        e->key = key;
        e->advance = (int16_t)adv;
        e->glyph = (uint16_t)g;
    }
    if (unscaledAdvance) *unscaledAdvance = (int16_t)adv;
    return g;
}

bool TtfFace::hasGlyph(uint32_t cp) const {
    if (cp == ' ' || cp == 0xA0) return true;
    return glyphOf(cp, nullptr) != 0;
}

int16_t TtfFace::advance(uint32_t cp, uint16_t sizePx, uint8_t) {
    int16_t adv = 0;
    if (glyphOf(cp, &adv) == 0 && cp == 0xA0) glyphOf(' ', &adv);   // a font may leave NBSP out
    return (int16_t)lroundf((float)adv * scale(sizePx));
}

int16_t TtfFace::lineHeight(uint16_t sizePx) {
    return (int16_t)lroundf((float)(_ascent - _descent + _lineGap) * scale(sizePx));
}

int16_t TtfFace::ascent(uint16_t sizePx) { return (int16_t)lroundf((float)_ascent * scale(sizePx)); }

int16_t TtfFace::kerning(uint32_t left, uint32_t right, uint16_t sizePx, uint8_t) {
    if (!_kerns) return 0;
    const int g1 = glyphOf(left, nullptr), g2 = glyphOf(right, nullptr);
    if (!g1 || !g2) return 0;
    TtfFamily* fam = _family;
    int kern;
    if (g1 < 0x8000 && g2 < 0x8000 && fam->_kerns) {
        const uint32_t key = ((uint32_t)_id << 30) | ((uint32_t)g1 << 15) | (uint32_t)g2;
        TtfFamily::KernEntry& e = fam->_kerns[mix(key) % TTF_KERN_SLOTS];
        if (e.key != key) {
            e.key = key;
            e.kern = (int16_t)stbtt_GetGlyphKernAdvance(_info, g1, g2);
        }
        kern = e.kern;
    } else {
        kern = stbtt_GetGlyphKernAdvance(_info, g1, g2);
    }
    return kern ? (int16_t)lroundf((float)kern * scale(sizePx)) : 0;
}

uint32_t TtfFace::ligature(uint32_t left, uint32_t right, uint8_t) {
    if (left != 'f') return 0;
    if (right == 'i' && _ligFi) return 0xFB01;
    if (right == 'l' && _ligFl) return 0xFB02;
    return 0;
}

const GlyphBitmap* TtfFace::rasterize(uint32_t cp, uint16_t sizePx) {
    TtfFamily* fam = _family;
    const int g = glyphOf(cp, nullptr);
    if (!g) return nullptr;
    int16_t adv = 0;
    glyphOf(cp, &adv);
    const bool cacheable = fam->_glyphs && sizePx < 128 && cp < (1u << 21);
    const uint32_t key = ((uint32_t)_id << 28) | ((uint32_t)sizePx << 21) | cp;
    TtfFamily::GlyphEntry* e = cacheable ? &fam->_glyphs[mix(key) % TTF_GLYPH_SLOTS] : nullptr;
    GlyphBitmap& out = fam->_glyph;
    out.advance = (int16_t)lroundf((float)adv * scale(sizePx));
    if (e && e->key == key && e->generation == fam->_generation) {
        // A hit: 1-bit rows back to 0/255 coverage.
        const uint32_t w = e->width, h = e->height, stride = (w + 7) / 8;
        uint8_t* px = fam->scratch(w * h > 0 ? w * h : 1);
        if (!px) return nullptr;
        const uint8_t* bits = fam->_bits + e->offset;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) px[y * w + x] = (bits[y * stride + x / 8] >> (7 - (x & 7))) & 1 ? 255 : 0;
        out.pixels = px;
        out.width = (uint16_t)w;
        out.height = (uint16_t)h;
        out.xoff = e->xoff;
        out.yoff = e->yoff;
        fam->_stats.glyphHits++;
        return &out;
    }
    const float s = scale(sizePx);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(_info, g, s, s, &x0, &y0, &x1, &y1);
    const int w = x1 > x0 ? x1 - x0 : 0, h = y1 > y0 ? y1 - y0 : 0;
    uint8_t* px = fam->scratch(w * h > 0 ? (size_t)w * h : 1);
    if (!px) return nullptr;
    if (w > 0 && h > 0) stbtt_MakeGlyphBitmap(_info, px, w, h, w, s, s, g);
    fam->_stats.rasterized++;
    out.pixels = px;
    out.width = (uint16_t)w;
    out.height = (uint16_t)h;
    out.xoff = (int16_t)x0;
    out.yoff = (int16_t)y0;
    // Into the cache, 1 bit a pixel (what a 1-bit page shows of it).
    if (e && w < 256 && h < 256 && x0 >= -128 && x0 < 128 && y0 >= -128 && y0 < 128) {
        const uint32_t stride = ((uint32_t)w + 7) / 8, bytes = stride * (uint32_t)h;
        if (bytes <= TTF_GLYPH_BITS_BYTES) {
            if (fam->_bitsUsed + bytes > TTF_GLYPH_BITS_BYTES) {
                // Full: a new generation (every entry empty), the store from the start.
                fam->_bitsUsed = 0;
                if (++fam->_generation == 0) {
                    fam->_generation = 1;
                    for (size_t i = 0; i < TTF_GLYPH_SLOTS; i++) fam->_glyphs[i].key = kEmpty;
                }
                fam->_stats.flushes++;
            }
            uint8_t* bits = fam->_bits + fam->_bitsUsed;
            memset(bits, 0, bytes);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (px[y * w + x] >= kInk) bits[y * stride + x / 8] |= (uint8_t)(0x80 >> (x & 7));
            e->key = key;
            e->offset = (uint16_t)fam->_bitsUsed;
            e->width = (uint8_t)w;
            e->height = (uint8_t)h;
            e->xoff = (int8_t)x0;
            e->yoff = (int8_t)y0;
            e->generation = fam->_generation;
            fam->_bitsUsed += bytes;
            // What is returned is what a hit returns: the same pixels on the page either way.
            for (int i = 0; i < w * h; i++) px[i] = px[i] >= kInk ? 255 : 0;
        }
    }
    return &out;
}
