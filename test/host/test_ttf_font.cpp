// TtfFamily (src/reader/engine/ttf_book_font.cpp) over the built-in family:
// metrics, kerning, ligatures, and the 1-bit glyph cache giving back what a
// fresh rasterization gives. Run: test/host/run.sh
#include <stdio.h>
#include <string.h>

#include <vector>

#include "reader/engine/ttf_book_font.h"
#include "reader/fonts/builtin_font.h"

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                  \
    do {                                                          \
        g_checks++;                                               \
        if (!(c)) {                                               \
            g_failed++;                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
        }                                                         \
    } while (0)

static std::vector<uint8_t> copyOf(const freeink::book::GlyphBitmap* g) {
    return g ? std::vector<uint8_t>(g->pixels, g->pixels + g->width * g->height) : std::vector<uint8_t>();
}

int main() {
    printf("  family %s\n", builtinfont::name());
    TtfFamily fam;
    for (int s = 0; s < 4; s++) {
        uint32_t len = 0;
        const uint8_t* d = builtinfont::face(s, &len);
        CHECK(fam.addFace((uint8_t)s, d, len));
        CHECK(fam.face((uint8_t)s) != nullptr);
    }
    TtfFace& f = *fam.face(0);
    CHECK(f.hasGlyph('A') && f.hasGlyph(0x0416) && f.hasGlyph(0x2014) && f.hasGlyph(0xA0));
    CHECK(!f.hasGlyph(0x4E2D));   // CJK: not in the cut
    CHECK(f.lineHeight(20) >= 22 && f.lineHeight(20) <= 30);
    CHECK(f.ascent(20) > 15 && f.ascent(20) < f.lineHeight(20));
    CHECK(f.advance('m', 20, 0) > f.advance('i', 20, 0));
    CHECK(f.advance(0xA0, 20, 0) == f.advance(' ', 20, 0));
    printf("  kerning AV at 40 px: %d, To: %d\n", f.kerning('A', 'V', 40, 0), f.kerning('T', 'o', 40, 0));
    CHECK(f.kerning('A', 'V', 40, 0) < 0 || f.kerning('T', 'o', 40, 0) < 0);
    CHECK(f.kerning('A', 'V', 40, 0) == f.kerning('A', 'V', 40, 0));   // from the cache the same
    if (f.hasGlyph(0xFB01)) CHECK(f.ligature('f', 'i', 0) == 0xFB01);
    CHECK(f.ligature('a', 'i', 0) == 0);
    // Fresh and cached glyphs are the same pixels (0/255).
    const std::vector<uint8_t> fresh = copyOf(f.rasterize(0x0416, 24));
    CHECK(!fresh.empty());
    bool binary = true;
    for (uint8_t v : fresh) binary = binary && (v == 0 || v == 255);
    CHECK(binary);
    const std::vector<uint8_t> cached = copyOf(f.rasterize(0x0416, 24));
    CHECK(fresh == cached);
    TtfFamily::Stats st = fam.takeStats();
    CHECK(st.rasterized == 1 && st.glyphHits == 1);
    // Faces differ: bold is wider than regular.
    CHECK(fam.face(1)->advance('m', 40, 0) >= f.advance('m', 40, 0));
    // Fill past the store: a flush, and glyphs still right afterwards.
    for (uint32_t cp = 0x20; cp < 0x7F; cp++)
        for (uint16_t px : {16, 20, 24, 30, 36, 48}) f.rasterize(cp, px);
    st = fam.takeStats();
    printf("  %u rasterized, %u flushes, caches %u B\n", st.rasterized, st.flushes, (unsigned)fam.cacheBytes());
    CHECK(st.flushes > 0);
    CHECK(copyOf(f.rasterize(0x0416, 24)) == fresh);
    // Released and filled again.
    fam.release();
    CHECK(fam.cacheBytes() == 0);
    CHECK(copyOf(f.rasterize(0x0416, 24)) == fresh);
    CHECK(fam.cacheBytes() > 0 && fam.cacheBytes() < 40 * 1024);
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
