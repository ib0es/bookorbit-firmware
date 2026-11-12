#include "book_reader_internal.h"

// ------------------------------------------------------------------ cover

bool BookReader::extractCover(const String& epubPath, const String& outPath, bool* noMemory, const String& cacheDir) {
    if (noMemory) *noMemory = false;
    bool oom = false;
    SdBookSource src;
    if (!src.open(epubPath)) return false;
    SdCacheStorage cache;
    Storage.mkdir("/cache");
    cache.setDir(cacheDir.length() ? cacheDir : cacheDirFor(epubPath));
    Storage.mkdir(cache.dir().c_str());
    // The book's index (built once, then used when the book is opened too).
    BookCatalog catalog;
    BookStatus st = BookStatus::Stale;
    size_t resident = 0;
    uint8_t* mem = nullptr;
    for (int attempt = 0; attempt < 2 && st == BookStatus::Stale; attempt++) {
        if ((attempt > 0 || !cache.exists(BookCatalog::kCatalogName)) && !buildContainerIndex(src, cache, oom)) break;
        if (BookCatalog::residentBytes(cache, &resident) != BookStatus::Ok) continue;
        free(mem);
        mem = (uint8_t*)malloc(resident + 64);
        Buffer scratch(4096);
        if (!mem || !scratch.p) { oom = true; break; }
        Arena bookArena(mem, resident + 64), s(scratch.p, 4096);
        catalog = BookCatalog();
        st = catalog.open(src, cache, bookArena, s);
    }
    bool ok = false;
    ZipEntry entry;
    if (st == BookStatus::Ok && catalog.coverEntry(&entry)) {
        // A stored image (the usual case) is copied; a deflated one needs an inflate stream.
        Buffer inflate(entry.method == 0 ? 1024 : kInflateBytes);
        if (!inflate.p) {
            oom = true;
        } else {
            Arena a(inflate.p, inflate.n);
            ZipEntryReader reader;
            Storage.mkdir("/covers");
            const String tmp = outPath + ".tmp";
            FsFile out = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
            if (out && reader.open(src, entry, a) == BookStatus::Ok) {
                uint8_t buf[1024];
                ok = true;
                for (int32_t n; ok && (n = reader.read(buf, sizeof(buf))) > 0;) ok = out.write(buf, (size_t)n) == (size_t)n;
                ok = ok && reader.totalProduced() == entry.uncompressedSize;
            }
            if (out) out.close();
            ok = ok && Storage.replaceFile(tmp.c_str(), outPath.c_str());
            if (!ok) Storage.remove(tmp.c_str());
        }
    }
    catalog = BookCatalog();
    free(mem);
    src.close();
    Serial.printf("[Reader] Cover of %s: %s\n", epubPath.c_str(),
                  ok ? outPath.c_str() : oom ? "no memory" : st != BookStatus::Ok ? "unreadable book" : "none");
    if (noMemory) *noMemory = oom;
    return ok;
}

