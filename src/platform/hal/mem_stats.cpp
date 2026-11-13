#include "mem_stats.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if __has_include(<freertos/idf_additions.h>)
#include <freertos/idf_additions.h>   // pxTaskGetStackStart()
#endif
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
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
    // The numbers above count PSRAM too. Wi-Fi, DMA and small blocks (<= 4 KB
    // with SPIRAM_USE_MALLOC) live in internal RAM: shown apart, to check the
    // large memory tier on the device.
    Serial.printf("[Mem] %s: internal free %u KB, largest %u KB, min %u KB; PSRAM free %u KB\n", where,
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#endif
}

bool probeEngine(const char* where) {
#if !BOOKORBIT_MEM_DIAG
    (void)where;
    return true;
#endif
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
    Serial
            .printf("[Mem] engine probe %s: layout 50 KB %s, parse 46 KB %s, book %u/30 KB; left free %u KB, "
                    "largest block %u KB -> %s\n",
                    where, layout ? "ok" : "NO", parse ? "ok" : "NO", (unsigned)(book / 1024),
                    (unsigned)(left.freeBytes / 1024), (unsigned)(left.largestBlock / 1024),
                    ok ? "FITS" : "DOES NOT FIT");
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
        for (int i = 0; i < N; i++)
            if (size > v[i]) {
                at = i;
                break;
            }
        if (at < 0) {
            rest++;
            restBytes += size;
            return;
        }
        if (v[N - 1]) {
            rest++;
            restBytes += v[N - 1];
        }
        for (int i = N - 1; i > at; i--) v[i] = v[i - 1];
        v[at] = size;
    }
    void print(char* out, size_t len) const {
        size_t n = 0;
        for (int i = 0; i < N && v[i] && n < len; i++)
            n += snprintf(out + n, len - n, i ? " %u" : "%u", (unsigned)((v[i] + 512) / 1024));
        if (n < len)
            snprintf(out + n, len - n, " KB (+%u smaller, %u KB)", (unsigned)rest, (unsigned)(restBytes / 1024));
    }
};

struct HeapMap {
    TopSizes freeB, usedB;
};

#if MEM_HEAP_WALK
bool walker(walker_heap_into_t, walker_block_info_t block, void* user) {
    HeapMap* m = static_cast<HeapMap*>(user);
    (block.used ? m->usedB : m->freeB).add((uint32_t)block.size);
    return true;
}
#endif

}  // namespace

void logMap(const char* where) {
#if !BOOKORBIT_MEM_DIAG
    (void)where;
#elif MEM_HEAP_WALK
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
                  (unsigned)info.free_blocks, (unsigned)info.allocated_blocks,
                  (unsigned)(info.largest_free_block / 1024));
#else
    Serial.printf("[Mem] map %s: (no heap walk in the simulator)\n", where);
#endif
}

namespace {

struct Big {
    uintptr_t p;
    uint32_t size;
};
constexpr int kMaxBig = 48;
constexpr uint32_t kBigBytes = 2048;
Big g_prev[kMaxBig];
int g_prevN = 0;
bool g_marked = false;
char g_marks[2048];
size_t g_marksLen = 0;

#if MEM_HEAP_WALK
struct BigWalk {
    Big b[kMaxBig];
    int n;
    uint32_t lost;
};
BigWalk g_walk;   // static: the walker runs with the heap locked, no allocation there

bool bigWalker(walker_heap_into_t, walker_block_info_t block, void*) {
    if (!block.used || block.size < kBigBytes) return true;
    if (g_walk.n < kMaxBig)
        g_walk.b[g_walk.n++] = Big{(uintptr_t)block.ptr, (uint32_t)block.size};
    else
        g_walk.lost++;
    return true;
}

// "stack loopTask" if the block holds a task's stack.
const char* ownerOf(const Big& b) {
    static const char* const kTasks[] = {"loopTask", "IDLE",      "IDLE0",   "esp_timer",      "ipc0",
                                         "sys_evt",  "tiT",       "wifi",    "arduino_events", "Tmr Svc",
                                         "fi_input", "async_tcp", "ble-conn"};
    static char buf[32];
    for (const char* name : kTasks) {
        TaskHandle_t h = xTaskGetHandle(name);
        if (!h) continue;
        const uintptr_t st = (uintptr_t)pxTaskGetStackStart(h);
        if (st >= b.p && st < b.p + b.size) {
            snprintf(buf, sizeof(buf), " (stack %s)", name);
            return buf;
        }
    }
    return "";
}
#endif

void appendMark(const char* fmt, ...) {
    if (g_marksLen >= sizeof(g_marks) - 1) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(g_marks + g_marksLen, sizeof(g_marks) - g_marksLen, fmt, ap);
    va_end(ap);
    if (n > 0) g_marksLen = std::min(sizeof(g_marks) - 1, g_marksLen + (size_t)n);
}

}  // namespace

void mark(const char* step) {
#if MEM_HEAP_WALK && BOOKORBIT_MEM_DIAG
    g_walk.n = 0;
    g_walk.lost = 0;
    heap_caps_walk(MALLOC_CAP_8BIT, bigWalker, nullptr);
    const Snapshot s = now();
    appendMark("[Mem] mark %-22s free %3u KB, largest %3u KB:", step, (unsigned)(s.freeBytes / 1024),
               (unsigned)(s.largestBlock / 1024));
    int listed = 0;
    for (int i = 0; i < g_walk.n; i++) {
        bool known = false;
        for (int j = 0; j < g_prevN && !known; j++)
            known = g_prev[j].p == g_walk.b[i].p && g_prev[j].size == g_walk.b[i].size;
        if (known && g_marked) continue;
        appendMark(" +%.1f%s", g_walk.b[i].size / 1024.0f, ownerOf(g_walk.b[i]));
        listed++;
    }
    for (int j = 0; j < g_prevN && g_marked; j++) {
        bool still = false;
        for (int i = 0; i < g_walk.n && !still; i++)
            still = g_prev[j].p == g_walk.b[i].p && g_prev[j].size == g_walk.b[i].size;
        if (!still) {
            appendMark(" -%.1f", g_prev[j].size / 1024.0f);
            listed++;
        }
    }
    if (!listed) appendMark(" no big block changed");
    if (g_walk.lost) appendMark(" (+%u not tracked)", (unsigned)g_walk.lost);
    appendMark("\n");
    memcpy(g_prev, g_walk.b, sizeof(Big) * g_walk.n);
    g_prevN = g_walk.n;
    g_marked = true;
#else
    (void)step;
#endif
}

void printMarks() {
    if (g_marksLen == 0) return;
    Serial.print("[Mem] big blocks (2 KB and more, KB) appearing at each boot step:\n");
    Serial.print(g_marks);
    g_marksLen = 0;
    g_marks[0] = 0;
}

String summary() {
    const Snapshot s = now();
    char buf[64];
    snprintf(buf, sizeof(buf), "RAM: %u KB (blk %u, min %u)", (unsigned)(s.freeBytes / 1024),
             (unsigned)(s.largestBlock / 1024), (unsigned)(s.minFreeEver / 1024));
    return String(buf);
}

}  // namespace MemStats
