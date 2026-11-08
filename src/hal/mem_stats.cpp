#include "mem_stats.h"

#include <cstdlib>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace MemStats {

Snapshot now() {
    Snapshot s;
#if defined(ESP_PLATFORM)
    s.freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    s.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    s.minFreeEver = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
#else
    s.freeBytes = s.largestBlock = s.minFreeEver = ESP.getFreeHeap();
#endif
    return s;
}

void log(const char* where) {
    const Snapshot s = now();
    Serial.printf("[Mem] %s: free %u KB, largest block %u KB, min since boot %u KB\n", where,
                  (unsigned)(s.freeBytes / 1024), (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
}

bool probeEngine(const char* where) {
    // Sizes from fibcheck on 27 real books (FREEINK_BOOK_SMALL): fixed layout peak
    // 95,333 bytes in two buffers, plus up to ~30 KB for an open book.
    constexpr size_t kLayout = 50 * 1024, kParse = 46 * 1024, kBook = 30 * 1024, kPiece = 2 * 1024;
    void* layout = malloc(kLayout);
    void* parse = malloc(kParse);
    void* pieces[kBook / kPiece + 1] = {};
    size_t book = 0;
    for (size_t i = 0; book < kBook && i < sizeof(pieces) / sizeof(pieces[0]); i++) {
        pieces[i] = malloc(kPiece);
        if (!pieces[i]) break;
        book += kPiece;
    }
    const bool ok = layout && parse && book >= kBook;
    const Snapshot left = now();   // what would remain for everything else
    for (void* p : pieces) free(p);
    free(parse);
    free(layout);
    Serial.printf("[Mem] engine probe %s: layout 50 KB %s, parse 46 KB %s, book %u/30 KB; left free %u KB, "
                  "largest block %u KB -> %s\n",
                  where, layout ? "ok" : "NO", parse ? "ok" : "NO", (unsigned)(book / 1024),
                  (unsigned)(left.freeBytes / 1024), (unsigned)(left.largestBlock / 1024), ok ? "FITS" : "DOES NOT FIT");
    return ok;
}

String summary() {
    const Snapshot s = now();
    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %u KB (blk %u, min %u)", (unsigned)(s.freeBytes / 1024),
             (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
    return String(buf);
}

}  // namespace MemStats
