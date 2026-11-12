#include "book_reader_internal.h"
#include <epub/ImageProbe.h>

namespace {

// A cover the cover viewer cannot decode itself (TJpgDec: baseline JPEG only)
// is drawn once by the book engine's image renderer (PNG, progressive JPEG) into
// an 8-bit gray BMP, which the viewer draws. Fitted into this box, never enlarged:
// a 1600x2400 cover would otherwise be megabytes and seconds of card writes.
constexpr uint16_t kCoverMaxW = 600;
constexpr uint16_t kCoverMaxH = 900;

// The renderer finds the image by name in a container: here a single raw file.
class OneImage : public ZipCatalog {
public:
    explicit OneImage(const ZipEntry& e) : _e(e) {}
    const ZipEntry* find(const char*) const override { return &_e; }
    const ZipEntry* findByHash(uint32_t) const override { return &_e; }
private:
    ZipEntry _e;
};

struct GrayBmp {
    FsFile out;
    uint16_t width = 0, height = 0, rows = 0;
    size_t stride = 0;
    uint8_t* row = nullptr;
    bool ok = true;

    bool put(const uint8_t* gray, uint16_t w) {
        memset(row, 0xFF, stride);
        if (gray) memcpy(row, gray, w < width ? w : width);
        if (out.write(row, stride) != stride) return false;
        rows++;
        return true;
    }
    static bool onRow(void* user, uint16_t y, const uint8_t* gray, uint16_t w) {
        GrayBmp* b = static_cast<GrayBmp*>(user);
        Platform::feedWatchdog();
        while (b->ok && b->rows < y && b->rows < b->height) b->ok = b->put(nullptr, 0);   // skipped rows: white
        if (b->ok && y == b->rows && b->rows < b->height) b->ok = b->put(gray, w);
        return b->ok;
    }
};

bool writeGrayBmpHeader(FsFile& out, uint16_t w, uint16_t h, size_t stride) {
    const uint32_t imageSize = (uint32_t)(stride * h), offset = 54 + 1024, fileSize = offset + imageSize;
    auto le32 = [](uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; };
    uint8_t hdr[54] = {'B', 'M'};
    le32(hdr + 2, fileSize);
    le32(hdr + 10, offset);
    le32(hdr + 14, 40);
    le32(hdr + 18, w);
    le32(hdr + 22, (uint32_t)-(int32_t)h);   // top-down rows
    hdr[26] = 1;
    hdr[28] = 8;
    le32(hdr + 34, imageSize);
    le32(hdr + 46, 256);
    if (out.write(hdr, 54) != 54) return false;
    uint8_t pal[64];
    for (int i = 0; i < 256; i += 16) {
        for (int k = 0; k < 16; k++) pal[k * 4] = pal[k * 4 + 1] = pal[k * 4 + 2] = (uint8_t)(i + k), pal[k * 4 + 3] = 0;
        if (out.write(pal, 64) != 64) return false;
    }
    return true;
}

enum class CoverKind { Drawable, Convert, Unusable };

// What the cover viewer makes of an image file: a baseline JPEG it draws, a PNG
// or progressive JPEG to convert first, anything else (GIF, WebP, SVG) not at all.
CoverKind coverKindOf(const String& path, ImageInfo& info) {
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return CoverKind::Unusable;
    const uint32_t size = (uint32_t)f.size();
    f.close();
    SdBookSource src;
    if (!src.open(path)) return CoverKind::Unusable;
    const ZipEntry raw = ZipEntryReader::rawEntry(size);
    Buffer scratch(4096);
    if (!scratch.p) return CoverKind::Unusable;
    Arena a(scratch.p, scratch.n);
    const BookStatus st = probeImage(src, raw, a, &info);
    src.close();
    if (st != BookStatus::Ok || info.width == 0 || info.height == 0) return CoverKind::Unusable;
    if (info.kind == ImageInfo::Kind::Jpeg && !info.progressive) return CoverKind::Drawable;
    if (info.kind == ImageInfo::Kind::Jpeg || info.kind == ImageInfo::Kind::Png) return CoverKind::Convert;
    return CoverKind::Unusable;
}

bool imageFileToGrayBmp(const String& imgPath, const ImageInfo& info, const String& bmpPath, bool& oom) {
    FsFile f = Storage.open(imgPath.c_str(), O_RDONLY);
    if (!f) return false;
    const uint32_t size = (uint32_t)f.size();
    f.close();

    uint32_t w = info.width, h = info.height;
    if (w > kCoverMaxW) { h = h * kCoverMaxW / w; w = kCoverMaxW; }
    if (h > kCoverMaxH) { w = w * kCoverMaxH / h; h = kCoverMaxH; }
    if (w == 0) w = 1;
    if (h == 0) h = 1;

    Buffer scratch(kImageBytes);
    if (!scratch.p) { oom = true; return false; }
    GrayBmp bmp;
    bmp.width = (uint16_t)w;
    bmp.height = (uint16_t)h;
    bmp.stride = ((w + 3) / 4) * 4;
    bmp.row = (uint8_t*)malloc(bmp.stride);
    if (!bmp.row) { oom = true; return false; }
    const String tmp = bmpPath + ".bmp.tmp";
    bmp.out = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    bool ok = bmp.out && writeGrayBmpHeader(bmp.out, bmp.width, bmp.height, bmp.stride);
    BookStatus st = BookStatus::Unsupported;
    if (ok) {
        SdBookSource src;
        if (src.open(imgPath)) {
            const OneImage one(ZipEntryReader::rawEntry(size));
            const PageImage place{"cover", 0, 0, bmp.width, bmp.height};
            Arena a(scratch.p, scratch.n);
            st = ImageRenderer::render(src, one, place, a, &GrayBmp::onRow, &bmp);
            src.close();
        }
        if (st == BookStatus::OutOfMemory) oom = true;
        ok = st == BookStatus::Ok && bmp.ok && bmp.rows > 0;
        // Rows the decoder did not give (a picture cut short) stay white.
        while (ok && bmp.rows < bmp.height) ok = bmp.put(nullptr, 0);
    }
    free(bmp.row);
    if (bmp.out) bmp.out.close();
    if (ok && Storage.replaceFile(tmp.c_str(), bmpPath.c_str())) return true;
    Storage.remove(tmp.c_str());
    Serial.printf("[Reader] Cover picture %ux%u not converted (%s)\n", (unsigned)info.width, (unsigned)info.height,
                  bookStatusName(st));
    return false;
}

bool containsNoCase(const char* s, const char* word) {
    const size_t n = strlen(word);
    for (; s && *s; ++s)
        if (strncasecmp(s, word, n) == 0) return true;
    return false;
}

bool endsWithNoCase(const char* s, const char* sfx) {
    const size_t a = strlen(s), b = strlen(sfx);
    return a >= b && strcasecmp(s + a - b, sfx) == 0;
}

// A book that declares no cover (no cover-image property, no EPUB 2 meta): an
// image whose path says "cover" (cover.jpg, Images/Cover.png ...). Only the ZIP
// directory is read, into an arena given back before the image is copied.
bool coverByName(SdBookSource& src, ZipEntry& out) {
    uint16_t count = 0;
    uint32_t dirOffset = 0, dirSize = 0;
    if (ZipCatalog::locateCentralDirectory(src, &dirOffset, &dirSize, &count) != BookStatus::Ok || count == 0) return false;
    const size_t want = (size_t)dirSize + (size_t)count * (sizeof(ZipEntry) + 8) + 256;
    if (want > 32 * 1024) return false;   // a huge book: no guessing
    Buffer mem(want);
    if (!mem.p) return false;
    Arena a(mem.p, mem.n);
    ZipCatalog zip;
    if (zip.open(src, a) != BookStatus::Ok) return false;
    for (size_t i = 0; i < zip.entryCount(); i++) {
        const ZipEntry* e = zip.entry(i);
        if (!e || !e->name || !containsNoCase(e->name, "cover")) continue;
        if (!endsWithNoCase(e->name, ".jpg") && !endsWithNoCase(e->name, ".jpeg") && !endsWithNoCase(e->name, ".png")) continue;
        out = *e;
        out.name = nullptr;   // lives in the arena
        return true;
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------------ cover

bool BookReader::extractCover(const String& epubPath, const String& outPath, bool* noMemory, const String& cacheDir) {
    Platform::feedWatchdog();
    if (noMemory) *noMemory = false;
    bool oom = false;
    SdBookSource src;
    if (!src.open(epubPath)) return false;
    SdCacheStorage cache;
    Storage.mkdir("/cache");
    cache.setDir(cacheDir.length() ? cacheDir : cacheDirFor(epubPath));
    Storage.mkdir(cache.dir().c_str());
    // The book's index (built once, then used when the book is opened too): the
    // cover is found by it (manifest property, EPUB 2 meta, or an image named
    // "cover"). No second parser here: one walk of the ZIP with growing Strings
    // next to an inflate stream cut the X4's heap so that books stopped opening.
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
    bool copied = false;
    ZipEntry entry;
    bool hasCover = st == BookStatus::Ok && catalog.coverEntry(&entry);
    // The catalog's arena is not needed to copy the entry: given back before the
    // inflate stream is taken, so that the two never sit in the heap together.
    entry.name = nullptr;   // lived in the catalog's arena; the reader needs offsets and sizes only
    catalog = BookCatalog();
    free(mem);
    mem = nullptr;
    if (st == BookStatus::Ok && !hasCover) hasCover = coverByName(src, entry);
    const String tmp = outPath + ".tmp";
    if (hasCover) {
        // A stored image (the usual case) is copied; a deflated one needs an inflate stream.
        Buffer inflate(entry.method == 0 ? 1024 : kInflateBytes);
        if (!inflate.p) {
            oom = true;
        } else {
            Arena a(inflate.p, inflate.n);
            ZipEntryReader reader;
            Storage.mkdir("/covers");
            FsFile out = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
            if (out && reader.open(src, entry, a) == BookStatus::Ok) {
                uint8_t buf[1024];
                copied = true;
                for (int32_t n; copied && (n = reader.read(buf, sizeof(buf))) > 0;) {
                    Platform::feedWatchdog();
                    copied = out.write(buf, (size_t)n) == (size_t)n;
                }
                copied = copied && reader.totalProduced() == entry.uncompressedSize;
            }
            if (out) out.close();
        }
    }
    src.close();
    // Looked at (and converted) after the inflate stream and the book are given back.
    bool ok = false;
    const char* how = "";
    if (copied) {
        ImageInfo info;
        switch (coverKindOf(tmp, info)) {
            case CoverKind::Drawable:
                ok = Storage.replaceFile(tmp.c_str(), outPath.c_str());
                break;
            case CoverKind::Convert:
                ok = imageFileToGrayBmp(tmp, info, outPath, oom);
                how = info.kind == ImageInfo::Kind::Png ? " (PNG as BMP)" : " (progressive JPEG as BMP)";
                break;
            case CoverKind::Unusable:
                how = " (not a picture it can draw)";
                break;
        }
    }
    Storage.remove(tmp.c_str());
    Serial.printf("[Reader] Cover of %s: %s%s\n", epubPath.c_str(),
                  ok ? outPath.c_str() : oom ? "no memory" : st != BookStatus::Ok ? "unreadable book" : "none", how);
    if (noMemory) *noMemory = oom && !ok;
    return ok;
}
