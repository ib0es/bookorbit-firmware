#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#include "EInkDisplay.h"
#include "InputManager.h"
#include "platform/hal/storage.h"
#include "services/library_manager.h"
#include "services/offline_metadata.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = {false};
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = {false};
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
void simPumpEvents() {}
void simInputPolled() {}
namespace fs = std::filesystem;
static fs::path root;
static void write(const char* path, const std::string& text) {
    const auto p = root / path;
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << text;
}
static std::string read(const char* path) {
    std::ifstream f(root / path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
static std::string fb2(const char* title) {
    return "<FictionBook><!--" + std::string(4000, 'x') + "--><description><title-info><book-title>" + title +
           "</book-title><author><first-name>Ada</first-name><last-name>Lovelace</last-name></author><sequence name='Цикл' number='1.5'/></title-info></"
           "description><body><p>" +
           std::string(15000, 'z') + "</p></body></FictionBook>";
}
static std::string embeddedBmp() {
    std::vector<uint8_t> bmp(374, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[2] = 118;
    bmp[3] = 1;
    bmp[10] = 54;
    bmp[14] = 40;
    bmp[18] = 10;
    bmp[22] = 10;
    bmp[26] = 1;
    bmp[28] = 24;
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bmp.size(); i += 3) {
        const uint32_t v = uint32_t(bmp[i]) << 16 | (i + 1 < bmp.size() ? uint32_t(bmp[i + 1]) << 8 : 0) |
                           (i + 2 < bmp.size() ? bmp[i + 2] : 0);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < bmp.size() ? alphabet[(v >> 6) & 63] : '=';
        out += i + 2 < bmp.size() ? alphabet[v & 63] : '=';
    }
    return "<FictionBook><description><title-info><book-title>Covered</book-title><coverpage><image "
           "href='#image'/></coverpage></title-info></description><body/><binary id='image'>" +
           out + "</binary></FictionBook>";
}
// Bytes the process holds in malloc now: a cycle of the library's offline lifecycle must not grow it.
static size_t heapInUse() {
#if defined(__APPLE__)
    malloc_statistics_t stats;
    malloc_zone_statistics(nullptr, &stats);
    return stats.size_in_use;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return mallinfo2().uordblks;
#else
    return static_cast<size_t>(mallinfo().uordblks);
#endif
}
static void drain() {
    unsigned idle = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        OfflineMetadata::tick();
        if (!OfflineMetadata::busy()) {
            if (++idle == 3) return;
        } else
            idle = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!"worker did not finish");
}
int main(int argc, char** argv) {
    assert(argc == 2);
    root = argv[1];
    fs::create_directories(root);
    SDCardManager::getInstance().setRoot(root.string());
    Storage.begin();
    write("offline/opaque.fb2", fb2("Alpha"));
    write("offline/folder/opaque.fb2", fb2("Other"));
    write("offline/broken.fb2", "<broken");
    write("offline/plain.txt", "plain text");
    write("offline/covered.fb2", embeddedBmp());
    write("offline/group.fb2", fb2("Grouped"));
    write("offline/group.txt", "same book in another format");
    write("progress.json", "positions stay unchanged");
    write("bookmarks/offline#opaque.fb2.json", "bookmarks stay unchanged");
    auto& lib = LibraryManager::getInstance();
    lib.currentFilter = FILTER_OFFLINE;
    lib.currentFolder = "/offline";
    lib.scanOfflineBooks();
    lib.rebuildFilteredIndices();
    OfflineMetadata::Record r;
    assert(!OfflineMetadata::load("/offline/opaque.fb2", r));
    drain();
    assert(OfflineMetadata::load("/offline/opaque.fb2", r));
    assert(r.title == "Alpha" && r.author == "Ada Lovelace");
    assert(r.series == "Цикл" && r.seriesIndex == 1.5);
    for (const auto& b : lib.offlineBooks)
        if (b.filePath == "/offline/opaque.fb2") assert(b.series == r.series && b.seriesIndex == 1.5);
    lib.scanOfflineBooks();
    for (const auto& b : lib.offlineBooks)
        if (b.filePath == "/offline/opaque.fb2") assert(b.series == r.series && b.seriesIndex == 1.5);
    // Version-1 negative/positive caches must not prevent new fields appearing.
    const auto oldCache = OfflineMetadata::cachePath("/offline/opaque.fb2");
    auto oldData = read(oldCache.c_str() + 1);
    const auto version = oldData.find("\"version\":2");
    assert(version != std::string::npos);
    oldData.replace(version, 11, "\"version\":1");
    write(oldCache.c_str() + 1, oldData);
    assert(!OfflineMetadata::load("/offline/opaque.fb2", r));
    OfflineMetadata::reset();
    drain();
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.series == "Цикл" && r.seriesIndex == 1.5);
    // Corrupt/untrusted series fields must never reach library rows from disk.
    const auto validData = read(oldCache.c_str() + 1);
    for (const std::string& fields : {std::string("\"series\":\"Bad\\u0001\",\"seriesIndex\":1.5,"),
                                      std::string("\"series\":\"\",\"seriesIndex\":1.5,"),
                                      std::string("\"series\":\"Saga\",\"seriesIndex\":\"1.5\","),
                                      std::string("\"series\":\"\xc0\xaf\",\"seriesIndex\":1.5,")}) {
        // ArduinoJson uses the last duplicate key, so append before the closing brace.
        auto corrupt = validData;
        corrupt.insert(corrupt.rfind('}'), "," + fields.substr(0, fields.size() - 1));
        write(oldCache.c_str() + 1, corrupt);
        assert(!OfflineMetadata::load("/offline/opaque.fb2", r));
    }
    write(oldCache.c_str() + 1, validData);
    const auto cache = OfflineMetadata::cachePath("/offline/opaque.fb2");
    assert(cache != OfflineMetadata::cachePath("/offline/folder/opaque.fb2"));
    assert(cache != OfflineMetadata::cachePath("/offline/opaque.epub"));
    assert(OfflineMetadata::load("/offline/broken.fb2", r) && r.title == "broken");
    assert(OfflineMetadata::load("/offline/plain.txt", r) && r.title == "plain");
    assert(OfflineMetadata::load("/offline/covered.fb2", r) && r.coverPath.length());
    assert(LibraryManager::isValidCoverFile(r.coverPath));
    assert(LibraryManager::resolveCoverPath("/offline/covered.fb2", "Covered", "", false) == r.coverPath);
    bool grouped = false;
    for (auto& b : lib.offlineBooks) {
        if (!b.filePath.startsWith("/offline/group.")) continue;
        grouped = b.formats.size() == 2;
        for (size_t i = 0; i < b.formats.size(); ++i) {
            LibraryManager::selectFormat(b, i);
            if (b.filePath.endsWith(".fb2"))
                assert(b.series == "Цикл" && b.seriesIndex == 1.5);
            else
                assert(!b.series.length() && b.seriesIndex == 0);
        }
    }
    assert(grouped);
    assert(read("progress.json") == "positions stay unchanged");
    assert(read("bookmarks/offline#opaque.fb2.json") == "bookmarks stay unchanged");
    assert(!fs::exists(root / "cache/offline#opaque.fb2.fib/book.epub"));
    // Same size, changed metadata outside the sampled regions: full worker fingerprint invalidates it.
    write("offline/opaque.fb2", fb2("Bravo"));
    OfflineMetadata::reset();
    drain();
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.title == "Bravo");
    assert(!OfflineMetadata::storeHints("/books/not-offline.fb2", "Bad", "", ""));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad\x01", "", ""));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "0000000000000000"));
    for (const char* invalid : {"NaN", "Infinity", "-1", "1e2", "1x", "1000001"})
        assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "", "Series", invalid));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "", "", "1.5"));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "", "Bad\x01", "1.5"));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "", String(std::string(513, 's').c_str()),
                                        "1.5"));
    assert(!OfflineMetadata::storeHints("/offline/opaque.fb2", "Bad", "", "", "", "\xc0\xaf", "1.5"));
    assert(OfflineMetadata::storeHints("/offline/opaque.fb2", "Legacy", "", ""));
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.series == "Цикл" && r.seriesIndex == 1.5);
    assert(OfflineMetadata::storeHints("/offline/opaque.fb2", "Browser title", "Browser author", "", "",
                                       "Browser series", "2.5"));
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.title == "Browser title");
    drain();
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.title == "Browser title");
    assert(r.series == "Browser series" && r.seriesIndex == 2.5);
    assert(OfflineMetadata::storeHints("/offline/opaque.fb2", "Browser title", "Browser author", ""));
    assert(OfflineMetadata::load("/offline/opaque.fb2", r) && r.series == "Browser series" && r.seriesIndex == 2.5);
    OfflineMetadata::reset();
    OfflineMetadata::tick();
    OfflineMetadata::stop();
    assert(!OfflineMetadata::busy());
    drain();
    // Cache invalidation never renames/removes source or touches reading state.
    OfflineMetadata::invalidate("/offline/opaque.fb2");
    assert(!Storage.exists(cache.c_str()));
    assert(fs::exists(root / "offline/opaque.fb2"));
    assert(read("progress.json") == "positions stay unchanged");
    assert(read("bookmarks/offline#opaque.fb2.json") == "bookmarks stay unchanged");
    lib.scanOfflineBooks();
    drain();
    fs::remove(root / "offline/opaque.fb2");
    lib.scanOfflineBooks();
    assert(!Storage.exists(cache.c_str()));
    assert(LibraryManager::resolveCoverPath("/offline/broken.fb2", "broken", "/covers/arbitrary.jpg", true) == "");
    // Deleting and rescanning books over and over (the portal's and the library's lifecycle) leaks nothing.
    for (int i = 0; i < 12; ++i)
        write(("offline/leak" + std::to_string(i) + ".fb2").c_str(), fb2(("Leak " + std::to_string(i)).c_str()));
    auto cycle = [&](int round) {
        lib.scanOfflineBooks();
        drain();
        const std::string victim = "offline/leak" + std::to_string(round % 12) + ".fb2";
        OfflineMetadata::invalidate("/" + String(victim.c_str()));
        fs::remove(root / victim);
        lib.scanOfflineBooks();
        write(victim.c_str(), fb2("Leak again"));
    };
    // Measured with the worker idle: a book half-indexed holds its buffers, which is not a leak.
    auto quiet = [&]() {
        drain();
        OfflineMetadata::stop();
        return heapInUse();
    };
    for (int i = 0; i < 24; ++i) cycle(i);   // warm-up: caches, pools, the first-time allocations
    const size_t before = quiet();
    for (int i = 0; i < 240; ++i) cycle(i);
    const size_t after = quiet();
    printf("Offline lifecycle heap: %zu -> %zu bytes after 240 delete/rescan cycles\n", before, after);
    assert(after < before + 16 * 1024);
    OfflineMetadata::stop();
    puts("Offline metadata integration: all passed");
}
