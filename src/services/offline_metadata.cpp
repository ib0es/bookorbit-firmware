#include "offline_metadata.h"

#include <ArduinoJson.h>

#include <atomic>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <new>

#include "../convert/cover_extract.h"
#include "../platform/hal/mem_stats.h"
#include "../storage/json_file.h"
#include "../core/ui_port.h"
#include "../platform/hal/background_task.h"
#include "../reader/book_reader.h"
#include "../reader/engine/sd_book_storage.h"
#include "library_internal.h"
#include "library_manager.h"

namespace OfflineMetadata {
namespace {
constexpr unsigned kVersion = 2;
constexpr uint64_t kMaxBook = 64 * 1024 * 1024;
constexpr size_t kMaxCacheFile = 12288;
constexpr uint32_t kMinLargestBlock = 64 * 1024, kMinFreeHeap = 96 * 1024;
Platform::BackgroundTask worker;
std::atomic<bool> cancelled{false};
bool running = false;
size_t nextBook = 0, nextFormat = 0;
String workPath;
Record result;
bool published = false, coverChanged = false;

bool isCancelled() { return cancelled.load(std::memory_order_relaxed); }
uint64_t hashBytes(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (n--) h = (h ^ *p++) * 1099511628211ULL;
    return h;
}
String hex(uint64_t h) {
    char b[17];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}
// Full content fingerprint is checked by the worker, including same-size overwrites.
// The small sampled fingerprint makes the foreground cache lookup independent of book size.
String fingerprint(SdBookSource& src, bool full) {
    uint64_t h = 14695981039346656037ULL, size = src.size();
    h = hashBytes(h, &size, sizeof(size));
    uint8_t buf[1024];
    if (full && size <= kMaxBook) {
        for (uint64_t p = 0; p < size;) {
            if (isCancelled()) return "";
            const uint32_t n = std::min<uint64_t>(sizeof(buf), size - p);
            if (src.readAt(p, buf, n) != (int32_t)n) return "";
            h = hashBytes(h, buf, n);
            p += n;
            if ((p & 0xffff) == 0) delay(1);
        }
    } else {
        for (uint64_t p : {uint64_t(0), size / 2, size > 1024 ? size - 1024 : uint64_t(0)}) {
            const uint32_t n = std::min<uint64_t>(sizeof(buf), size - p);
            if (src.readAt(p, buf, n) != (int32_t)n) return "";
            h = hashBytes(h, buf, n);
        }
    }
    return hex(h);
}
bool validText(const String& s) {
    if (s.length() > 512) return false;
    const uint8_t* p = (const uint8_t*)s.c_str();
    for (size_t i = 0; i < s.length();) {
        const uint8_t c = p[i++];
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        const unsigned n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!n || i + n > s.length()) return false;
        uint32_t cp = c & ((1 << (6 - n)) - 1);
        for (unsigned j = 0; j < n; ++j) {
            if ((p[i] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (p[i++] & 63);
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000))
            return false;
    }
    return true;
}
bool readCache(const String& path, JsonDocument& doc) {
    // Parsed straight from the card: no 12 KB buffer (the X4's largest free block is often under 20 KB).
    const String file = cachePath(path);
    FsFile f = Storage.open(file.c_str(), O_RDONLY);
    if (!f) return false;
    const size_t size = f.size();
    f.close();
    if (size == 0 || size >= kMaxCacheFile || JsonFile::read(file.c_str(), doc)) return false;
    const String title = doc["title"].as<const char*>(), author = doc["author"].as<const char*>();
    const String cover = doc["cover"].as<const char*>();
    if (!doc["series"].isNull() && !doc["series"].is<const char*>()) return false;
    if (!doc["seriesIndex"].isNull() && !doc["seriesIndex"].is<double>()) return false;
    const String series = doc["series"].as<const char*>();
    const double index = doc["seriesIndex"].as<double>();
    return doc["version"].as<unsigned>() == kVersion && path == doc["path"].as<const char*>() && title.length() &&
           validText(title) && validText(author) && validText(series) && std::isfinite(index) && index >= 0 &&
           index <= 1000000 && (series.length() || index == 0) &&
           (!cover.length() || cover == cachePath(path) + ".jpg");
}
void fromCache(const JsonDocument& doc, Record& r) {
    r.title = doc["title"].as<const char*>();
    r.author = doc["author"].as<const char*>();
    r.coverPath = doc["cover"].as<const char*>();
    r.series = doc["series"].as<const char*>();
    r.seriesIndex = r.series.length() ? doc["seriesIndex"].as<double>() : 0;
}
void work(void*) {
    published = false;
    coverChanged = false;
    result = Record();
    SdBookSource src;
    if (!src.open(workPath)) return;
    const String sample = fingerprint(src, false), content = fingerprint(src, true);
    if (!sample.length() || !content.length() || isCancelled()) {
        src.close();
        return;
    }
    JsonDocument old;
    const bool cached = readCache(workPath, old);
    if (cached && content == old["content"].as<const char*>()) {
        fromCache(old, result);
        src.close();
        published = true;
        return;
    }
    // A parse that cannot get its 56 KB inflate arena would be cached as "no metadata" for good:
    // under memory pressure leave the book unindexed and try again at a quieter moment.
    if (MemStats::now().largestBlock < kMinLargestBlock) {
        src.close();
        return;
    }
    const bool hints = cached && old["browser"].as<bool>() && !old["content"].as<String>().length() &&
                       sample == old["sample"].as<const char*>();
    if (hints)
        fromCache(old, result);
    else
        result = extract(src, workPath, isCancelled);
    const uint64_t size = src.size();
    src.close();
    if (isCancelled()) return;
    // Each exact path (including format extension) owns its image, never a neighbouring image.
    const String cover = cachePath(workPath) + ".jpg";
    if (!hints) {
        Storage.remove(cover.c_str());
        const String target = cachePath(workPath);
        LibraryInternal::removeCoverStem(target.substring(target.lastIndexOf('/') + 1));
        // Retire the old unversioned offline cover and its failure/render markers.
        LibraryInternal::removeCoverStem(LibraryInternal::stemOf(LibraryInternal::epubCoverPath(workPath)));
    }
    String lower = workPath;
    lower.toLowerCase();
    bool extracted = hints && Storage.exists(cover.c_str());
    if (!hints && size <= 16 * 1024 * 1024) {
        if (lower.endsWith(".epub"))
            extracted = BookReader::extractCover(workPath, cover, nullptr, "", true, isCancelled);
        else
            extracted = convert::extractDirectCover(workPath, cover, isCancelled);
    }
    if (isCancelled()) {
        Storage.remove(cover.c_str());
        return;
    }
    if (extracted && !LibraryManager::isValidCoverFile(cover))
        extracted = BookReader::convertCoverImage(cover, cover, nullptr, isCancelled);
    if (isCancelled()) {
        Storage.remove(cover.c_str());
        return;
    }
    if (extracted && LibraryManager::isValidCoverFile(cover))
        result.coverPath = cover;
    else {
        result.coverPath = "";
        Storage.remove(cover.c_str());
    }
    coverChanged = !hints && (result.coverPath.length() || old["cover"].as<String>().length());
    JsonDocument doc;
    doc["version"] = kVersion;
    doc["path"] = workPath;
    doc["sample"] = sample;
    doc["content"] = content;
    doc["title"] = result.title;
    doc["author"] = result.author;
    doc["cover"] = result.coverPath;
    doc["series"] = result.series;
    doc["seriesIndex"] = result.seriesIndex;
    Storage.mkdir("/cache");
    Storage.mkdir("/cache/offmeta");
    // Serialized through a small buffer and swapped in only when complete.
    if (!isCancelled() && measureJson(doc) < kMaxCacheFile) JsonFile::write(cachePath(workPath).c_str(), doc);
    published = true;
    if (const uint32_t left = MemStats::stackLeft())
        Serial.printf("[Offline] %s indexed, worker stack high-water mark: %u B left of 8192\n", workPath.c_str(),
                      (unsigned)left);
}
}  // namespace
String cachePath(const String& path) {
    return "/cache/offmeta/offmeta-" + hex(hashBytes(14695981039346656037ULL, path.c_str(), path.length())) + ".json";
}
bool load(const String& path, Record& out) {
    if (busy()) return false;
    JsonDocument doc;
    SdBookSource src;
    if (!readCache(path, doc) || !src.open(path)) return false;
    const String sample = fingerprint(src, false);
    src.close();
    if (!sample.length() || sample != doc["sample"].as<const char*>()) return false;
    fromCache(doc, out);
    if (out.title.length() > 512 || out.author.length() > 512 || !out.title.length()) return false;
    if (out.coverPath != cachePath(path) + ".jpg" || !Storage.exists(out.coverPath.c_str())) out.coverPath = "";
    return true;
}
void stop() {
    if (!running) return;
    cancelled.store(true, std::memory_order_relaxed);
    worker.join();
    running = false;
    // Resume this path on the next idle period, rather than losing a cancelled job.
    if (nextFormat) --nextFormat;
    workPath = "";
    result = Record();
    published = false;
}
void reset() {
    stop();
    nextBook = nextFormat = 0;
    if (!Storage.isAvailable()) return;
    FsFile dir = Storage.open("/cache/offmeta", O_RDONLY);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }
    FsFile entry;
    char name[256];
    while (entry.openNext(&dir, O_RDONLY)) {
        const bool directory = entry.isDirectory();
        entry.getName(name, sizeof(name));
        entry.close();
        if (directory) continue;
        const String n(name);
        const String target = "/cache/offmeta/" + n;
        if (n.endsWith(".tmp") || n.endsWith(".part")) {
            Storage.remove(target.c_str());
            continue;
        }
        if (n.endsWith(".json.jpg") && !Storage.exists(target.substring(0, target.length() - 4).c_str())) {
            Storage.remove(target.c_str());
            LibraryInternal::removeCoverStem(n.substring(0, n.length() - 4));
            continue;
        }
        if (!n.endsWith(".json")) continue;
        FsFile f = Storage.open(target.c_str(), O_RDONLY);
        const size_t size = f ? f.size() : 0;
        if (f) f.close();
        JsonDocument doc;
        if (!size || size >= kMaxCacheFile || JsonFile::read(target.c_str(), doc)) {
            Storage.remove(target.c_str());
            Storage.remove((target + ".jpg").c_str());
            LibraryInternal::removeCoverStem(n);
            continue;
        }
        const String path = doc["path"].as<const char*>();
        if (!LibraryManager::isOfflinePath(path) || cachePath(path) != target || !Storage.exists(path.c_str())) {
            Storage.remove(target.c_str());
            Storage.remove((target + ".jpg").c_str());
            LibraryInternal::removeCoverStem(n);
        }
    }
    dir.close();
}
bool busy() { return running; }
bool storeHints(const String& path, const String& title, const String& author, const String& coverTmp,
                const String& expectedSample, const String& series, const String& seriesIndex) {
    stop();
    String lower = path;
    lower.toLowerCase();
    double position = 0;
    if (!LibraryManager::isOfflinePath(path) || lower.endsWith(".txt") || !validText(title) || !validText(author) ||
        !validText(series) || !parseSeriesIndex(seriesIndex, position) || (!series.length() && seriesIndex.length()))
        return false;
    SdBookSource src;
    if (!src.open(path)) return false;
    const String sample = fingerprint(src, false);
    src.close();
    if (!sample.length() || (expectedSample.length() && sample != expectedSample)) return false;
    const String cover = cachePath(path) + ".jpg";
    if (coverTmp.length()) {
        FsFile f = Storage.open(coverTmp.c_str(), O_RDONLY);
        const uint32_t size = f ? f.size() : 0;
        uint8_t h[9] = {};
        bool ok = size >= 300 && size <= 64 * 1024 && f.read(h, 2) == 2 && h[0] == 255 && h[1] == 216;
        bool frame = false, scan = false;
        uint32_t at = 2;
        for (unsigned guard = 0; ok && guard < 128 && at + 4 < size; ++guard) {
            ok = f.seek(at) && f.read(h, 4) == 4 && h[0] == 255;
            if (!ok) break;
            if (h[1] == 255) {
                ++at;
                continue;
            }
            if (h[1] == 218) {
                scan = true;
                break;
            }
            const uint32_t n = (uint32_t(h[2]) << 8) | h[3];
            if (n < 2 || n > size - at - 2) {
                ok = false;
                break;
            }
            if (h[1] >= 0xc0 && h[1] <= 0xcf && h[1] != 0xc4 && h[1] != 0xc8 && h[1] != 0xcc) {
                ok = h[1] == 0xc0 && n >= 8 && f.read(h + 4, 5) == 5;
                const unsigned height = unsigned(h[5]) << 8 | h[6], width = unsigned(h[7]) << 8 | h[8];
                ok = ok && h[4] == 8 && width && height && width <= 600 && height <= 900;
                frame = ok;
            }
            at += 2 + n;
        }
        ok = ok && frame && scan && f.seek(size - 2) && f.read(h, 2) == 2 && h[0] == 255 && h[1] == 217;
        if (f) f.close();
        if (!ok || !Storage.replaceFile(coverTmp.c_str(), cover.c_str())) return false;
    }
    JsonDocument doc;
    doc["version"] = kVersion;
    doc["path"] = path;
    doc["sample"] = sample;
    doc["content"] = "";
    doc["browser"] = true;
    doc["title"] = title.length() ? title : fallbackTitle(path);
    doc["author"] = author;
    doc["cover"] = Storage.exists(cover.c_str()) ? cover : String();
    // Old portals omit series; preserve existing/native values rather than hiding them.
    Record native;
    if (!series.length() && !load(path, native)) {
        SdBookSource source;
        if (source.open(path)) native = extract(source, path);
        source.close();
    }
    doc["series"] = series.length() ? series : native.series;
    doc["seriesIndex"] = series.length() ? position : native.seriesIndex;
    Storage.mkdir("/cache");
    Storage.mkdir("/cache/offmeta");
    const String target = cachePath(path);
    if (measureJson(doc) >= kMaxCacheFile || !JsonFile::write(target.c_str(), doc)) return false;
    const String targetStem = target.substring(target.lastIndexOf('/') + 1);
    LibraryInternal::removeCoverStem(targetStem);
    Ui::coverCacheChanged();
    nextBook = nextFormat = 0;
    return true;
}
void invalidate(const String& path, bool removed) {
    stop();
    Storage.remove(cachePath(path).c_str());
    Storage.remove((cachePath(path) + ".tmp").c_str());
    Storage.remove((cachePath(path) + ".jpg").c_str());
    // Rendered thumbnails use the image's filename stem.
    const String p = cachePath(path);
    const String stem = p.substring(p.lastIndexOf('/') + 1);
    LibraryInternal::removeCoverStem(stem);
    // Not reset(): that walks and parses every record of the cache, and the portal deletes books one by one.
    // Orphans are swept by the next library rescan. Nor a restart from the first book: deleting books
    // one after another would fingerprint the whole catalog again after each of them.
    auto& books = LibraryManager::getInstance().offlineBooks;
    for (size_t i = 0; i < books.size() && i <= nextBook; ++i)
        for (const auto& f : books[i].formats)
            if (LibraryManager::formatPath(books[i], f) == path) {
                if (removed && i < nextBook)
                    --nextBook;  // the list closes up over it: the queue stays on the same next book
                else {
                    nextBook = i;  // a replaced file, or the book being indexed: its turn comes again
                    nextFormat = 0;
                }
                return;
            }
}
bool tick(bool startNext) {
    auto& lib = LibraryManager::getInstance();
    if (running) {
        if (!worker.finished()) return false;
        worker.join();
        running = false;
        if (published) {
            bool changed = coverChanged;
            for (auto& b : lib.offlineBooks)
                if (b.filePath == workPath) {
                    changed = changed || b.title != result.title || b.author != result.author ||
                              b.coverPath != result.coverPath || b.series != result.series ||
                              b.seriesIndex != result.seriesIndex;
                    b.title = result.title;
                    b.author = result.author;
                    b.coverPath = result.coverPath;
                    b.series = result.series;
                    b.seriesIndex = result.seriesIndex;
                }
            if (lib.lastBook.filePath == workPath) {
                changed = changed || lib.lastBook.title != result.title || lib.lastBook.author != result.author ||
                          lib.lastBook.coverPath != result.coverPath;
                lib.lastBook.title = result.title;
                lib.lastBook.author = result.author;
                lib.lastBook.coverPath = result.coverPath;
            }
            if (changed) {
                const String selected = lib.selectedBookIndex >= 0 && lib.selectedBookIndex < lib.rowCount()
                                                ? lib.rowAt(lib.selectedBookIndex).filePath
                                                : String();
                lib.rebuildFilteredIndices();
                for (int i = 0; i < lib.rowCount(); ++i)
                    if (lib.rowAt(i).filePath == selected) {
                        lib.selectedBookIndex = i;
                        break;
                    }
                Ui::coverCacheChanged();
                lib.touch();
                return true;
            }
        }
    }
    if (!startNext || !Storage.isAvailable()) return false;
    // Registered after the library/card singletons exist: join before their
    // destructors on a normal simulator exit, not after their storage is gone.
    static const bool exitHook = std::atexit(stop) == 0;
    if (!exitHook) return false;
    while (nextBook < lib.offlineBooks.size()) {
        const auto& b = lib.offlineBooks[nextBook];
        if (nextFormat >= b.formats.size()) {
            ++nextBook;
            nextFormat = 0;
            continue;
        }
        // Indexing is a convenience: never at the cost of the heap the portal, Wi-Fi and the reader need.
        // The worker takes an 8 KB stack plus parser buffers; wait for a quiet, unfragmented heap.
        const MemStats::Snapshot heap = MemStats::now();
        if (heap.largestBlock < kMinLargestBlock || heap.freeBytes < kMinFreeHeap) {
            static uint32_t lastLog = 0;
            if (millis() - lastLog > 30000) {
                lastLog = millis();
                MemStats::log("offline metadata skipped: low heap");
            }
            return false;
        }
        workPath = LibraryManager::formatPath(b, b.formats[nextFormat++]);
        cancelled.store(false, std::memory_order_relaxed);
        running = worker.start(work, nullptr);
        if (!running && nextFormat) --nextFormat;
        return false;
    }
    return false;
}
}  // namespace OfflineMetadata
