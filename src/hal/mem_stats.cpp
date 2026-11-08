#include "mem_stats.h"

#include <cstdlib>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#define MEM_HEAP_WALK 1   // heap_caps_walk()
#endif
#endif
#ifndef MEM_HEAP_WALK
#define MEM_HEAP_WALK 0
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

namespace {

// The N biggest of a stream of sizes (filled from the heap walker: no allocation there).
struct TopSizes {
    static constexpr int N = 8;
    uint32_t v[N] = {};
    uint32_t rest = 0;      // count not in v
    uint32_t restBytes = 0;
    void add(uint32_t size) {
        int at = -1;
        for (int i = 0; i < N; i++) if (size > v[i]) { at = i; break; }
        if (at < 0) { rest++; restBytes += size; return; }
        if (v[N - 1]) { rest++; restBytes += v[N - 1]; }
        for (int i = N - 1; i > at; i--) v[i] = v[i - 1];
        v[at] = size;
    }
    void print(char* out, size_t len) const {
        size_t n = 0;
        for (int i = 0; i < N && v[i] && n < len; i++)
            n += snprintf(out + n, len - n, i ? " %u" : "%u", (unsigned)((v[i] + 512) / 1024));
        if (n < len) snprintf(out + n, len - n, " KB (+%u smaller, %u KB)", (unsigned)rest, (unsigned)(restBytes / 1024));
    }
};

struct HeapMap { TopSizes freeB, usedB; };

#if MEM_HEAP_WALK
bool walker(walker_heap_into_t, walker_block_info_t block, void* user) {
    HeapMap* m = static_cast<HeapMap*>(user);
    (block.used ? m->usedB : m->freeB).add((uint32_t)block.size);
    return true;
}
#endif

}  // namespace

void logMap(const char* where) {
#if MEM_HEAP_WALK
    static HeapMap map;   // static: the walk must not allocate
    map = HeapMap();
    heap_caps_walk(MALLOC_CAP_8BIT, walker, &map);
    char f[160], u[160];
    map.freeB.print(f, sizeof(f));
    map.usedB.print(u, sizeof(u));
    Serial.printf("[Mem] map %s: free blocks %s; used blocks %s\n", where, f, u);
#elif defined(ESP_PLATFORM)
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    Serial.printf("[Mem] map %s: %u free blocks, %u used blocks, largest free %u KB\n", where,
                  (unsigned)info.free_blocks, (unsigned)info.allocated_blocks, (unsigned)(info.largest_free_block / 1024));
#else
    Serial.printf("[Mem] map %s: (no heap walk in the simulator)\n", where);
#endif
}

String summary() {
    const Snapshot s = now();
    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %u KB (blk %u, min %u)", (unsigned)(s.freeBytes / 1024),
             (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
    return String(buf);
}

}  // namespace MemStats
