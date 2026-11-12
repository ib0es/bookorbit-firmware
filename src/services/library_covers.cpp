#include "library_manager.h"
#include "library_internal.h"
#include "../core/progress_sync.h"
#include "sync_clock.h"
#include "../reader/reader.h"
#include "../convert/sd_convert.h"
#include "../convert/cover_extract.h"
#include "../reader/book_reader.h"
#include "../core/ui_port.h"
#include "../net/bookorbit_client.h"
#include "../core/book_formats.h"
#include "../ui/i18n.h"
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include "../storage/catalog_store.h"
#include "../storage/last_book_store.h"

using namespace LibraryInternal;

// P7: a server cover the renderer cannot draw (PNG, WebP, progressive JPEG)
// was downloaded and removed again at every sync. "<cover>.bad" holds the
// address it came from: the same address is not fetched again; a new one is.
bool LibraryManager::coverKnownUnusable(const String& coverPath, const String& coverUrl) {
    const String marker = coverPath + ".bad";
    if (coverUrl.length() == 0 || !Storage.exists(marker.c_str())) return false;
    FsFile f = Storage.open(marker.c_str(), O_RDONLY);
    if (!f) return false;
    char buf[256];
    const int n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    f.close();
    buf[n > 0 ? n : 0] = '\0';
    return coverUrl.equals(buf);
}

void LibraryManager::markCoverUnusable(const String& coverPath, const String& coverUrl) {
    const String marker = coverPath + ".bad";
    FsFile f = Storage.open(marker.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return;
    f.write((const uint8_t*)coverUrl.c_str(), coverUrl.length());
    f.close();
    Serial.printf("[Library] Cover marked unusable (not fetched again): %s\n", coverPath.c_str());
}

bool LibraryManager::isValidCoverFile(const String& path) {
    if (path.length() == 0 || !Storage.exists(path.c_str())) return false;
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return false;
    size_t sz = f.size();
    if (sz < 300) {
        f.close();
        return false;
    }
    uint8_t magic[4] = {0};
    f.read(magic, 4);
    f.close();

    bool isJpeg = (magic[0] == 0xFF && magic[1] == 0xD8);
    bool isBmp  = (magic[0] == 'B' && magic[1] == 'M');
    if ((!isJpeg && !isBmp) || (isJpeg && isProgressiveJpeg(path))) {
        // Not something the cover renderer can draw (PNG, WebP, an error page,
        // a progressive JPEG): drop it so a usable cover is fetched / extracted.
        Serial.printf("[Library] Unusable cover removed: %s\n", path.c_str());
        Storage.remove(path.c_str());
        return false;
    }
    return true;
}

// True if the JPEG is progressive (SOF2/6/10/14): TJpgDec draws baseline only.
// Walks the marker segments up to the first start-of-frame.
bool LibraryManager::isProgressiveJpeg(const String& path) {
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return false;
    uint32_t pos = 2;   // after FF D8
    const uint32_t size = f.size();
    bool progressive = false;
    for (int guard = 0; guard < 64 && pos + 4 <= size; guard++) {
        uint8_t h[4];
        if (!f.seek(pos) || f.read(h, 4) != 4 || h[0] != 0xFF) break;
        const uint8_t m = h[1];
        if (m == 0xFF) { pos++; continue; }                       // fill byte
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {   // SOFn
            progressive = (m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE);
            break;
        }
        if (m == 0xD9 || m == 0xDA) break;                        // end / scan before any frame
        pos += 2 + ((uint32_t)h[2] << 8 | h[3]);
    }
    f.close();
    return progressive;
}

// Where an EPUB's own cover goes: /covers/<file name without extension>.jpg (with
// its folders for a book outside /books, BookReader::cacheKeyFor).
String LibraryInternal::epubCoverPath(const String& filePath) {
    String base = BookReader::cacheKeyFor(filePath);
    const int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    return "/covers/" + base + ".jpg";
}

bool LibraryInternal::isEpubPath(const String& filePath) {
    String lower = filePath;
    lower.toLowerCase();
    return lower.endsWith(".epub");
}

String LibraryManager::resolveCoverPath(const String& filePath, const String& title, const String& explicitCoverPath, bool allowExtract) {
    if (isValidCoverFile(explicitCoverPath)) {
        return explicitCoverPath;
    }

    // 1. Check next to the book file in its folder (e.g. cover.jpg, Cover.jpg, <basename>.jpg)
    if (filePath.length() > 0) {
        int lastSlash = filePath.lastIndexOf('/');
        if (lastSlash >= 0) {
            String dir = filePath.substring(0, lastSlash + 1);
            String base = filePath.substring(lastSlash + 1);
            int dot = base.lastIndexOf('.');
            if (dot > 0) base = base.substring(0, dot);

            const char* candidates[] = {
                "cover.jpg", "Cover.jpg", "cover.jpeg", "Cover.jpeg",
                "cover.bmp", "Cover.bmp", "folder.jpg", "folder.jpeg"
            };
            for (const char* c : candidates) {
                String p = dir + c;
                if (isValidCoverFile(p)) return p;
            }
            if (base.length() > 0) {
                String p1 = dir + base + ".jpg";
                if (isValidCoverFile(p1)) return p1;
                String p2 = dir + base + ".jpeg";
                if (isValidCoverFile(p2)) return p2;
                String p3 = dir + base + ".bmp";
                if (isValidCoverFile(p3)) return p3;
            }
        }
    }

    // 2. /covers/<file name>.jpg: the server's cover of a downloaded book, or the
    //    one extracted from the EPUB (both by the file's own name, never by the
    //    title: two books of one title do not share a cover).
    if (filePath.length() > 0) {
        String cPath = epubCoverPath(filePath);
        // "This book has no cover it can draw": written by a firmware that converts
        // PNG and progressive JPEG covers (content "2"). An older marker (empty: a
        // failure for any reason, or a cover it could not convert yet) is dropped
        // and the cover looked for again.
        String noCoverMarker = cPath + ".nocover";
        {
            FsFile m = Storage.open(noCoverMarker.c_str(), O_RDONLY);
            if (m) {
                char v = 0;
                const bool current = m.read(&v, 1) == 1 && v == '2';
                m.close();
                if (current) return "";
                Storage.remove(noCoverMarker.c_str());
            }
        }
        if (isValidCoverFile(cPath)) return cPath;

        String cPathJpeg = cPath;
        if (cPathJpeg.endsWith(".jpg")) {
            cPathJpeg = cPathJpeg.substring(0, cPathJpeg.length() - 4) + ".jpeg";
            if (isValidCoverFile(cPathJpeg)) return cPathJpeg;
            String cPathBmp = cPath.substring(0, cPath.length() - 4) + ".bmp";
            if (isValidCoverFile(cPathBmp)) return cPathBmp;
        }

        // 3. Extract embedded cover from EPUB if allowed. A cover that cannot be
        //    drawn (progressive JPEG, PNG) is not extracted again this session:
        //    the home screen asks for it on every redraw.
        // P4: by a hash of the path in a fixed ring, not a growing vector of
        // long-lived Strings on the heap.
        static uint32_t s_unusableEpubCovers[16] = {};
        static int s_unusableNext = 0;
        uint32_t cHash = 2166136261u;
        for (size_t i = 0; i < cPath.length(); i++) cHash = (cHash ^ (uint8_t)cPath[i]) * 16777619u;
        if (!cHash) cHash = 1;
        bool knownBad = false;
        for (uint32_t h : s_unusableEpubCovers) if (h == cHash) { knownBad = true; break; }
        // A converted book (FB2/MOBI/AZW3) has its EPUB in its cache directory once it was opened.
        String epub = filePath, indexDir;
        const bool conv = convert::needsConversion(filePath);
        if (conv) {
            indexDir = BookReader::cacheDirFor(filePath);
            epub = indexDir + "/book.epub";
        }
        if (allowExtract && !knownBad) {
            bool noMemory = false;
            bool extracted = false;
            if ((isEpubPath(filePath) || conv) && Storage.exists(epub.c_str())) {
                extracted = BookReader::extractCover(epub, cPath, &noMemory, indexDir);
            } else if (conv && Storage.exists(filePath.c_str())) {
                extracted = convert::extractDirectCover(filePath, cPath);
            }
            if (extracted && isValidCoverFile(cPath)) return cPath;
            if ((isEpubPath(filePath) || conv) && !noMemory) {   // short of memory now: tried again later
                s_unusableEpubCovers[s_unusableNext] = cHash;
                s_unusableNext = (s_unusableNext + 1) % (int)(sizeof(s_unusableEpubCovers) / sizeof(s_unusableEpubCovers[0]));
                FsFile nc = Storage.open(noCoverMarker.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
                if (nc) {
                    nc.write((const uint8_t*)"2", 1);
                    nc.close();
                }
            }
        }
    }

    return "";
}

namespace {
struct CoverMemo {
    uint32_t gen = 0;
    uint32_t key = 0;
    char path[160];
};
CoverMemo g_coverMemo[8];
int g_coverNext = 0;

uint32_t coverKey(const String& a, const String& b, const String& c) {
    uint32_t h = 2166136261u;   // FNV-1a over the three, separated
    for (const String* s : {&a, &b, &c}) {
        for (size_t i = 0; i < s->length(); i++) h = (h ^ (uint8_t)(*s)[i]) * 16777619u;
        h = (h ^ 0xFF) * 16777619u;
    }
    return h ? h : 1;
}
}  // namespace

String LibraryManager::recentCover(const String& filePath, const String& title, const String& coverPath) {
    const uint32_t key = coverKey(filePath, title, coverPath);
    for (const CoverMemo& m : g_coverMemo) {
        if (m.key == key && m.gen == _gen) return String(m.path);
    }
    const String found = resolveCoverPath(filePath, title, coverPath, true);
    if (found.length() < sizeof(g_coverMemo[0].path)) {
        CoverMemo& m = g_coverMemo[g_coverNext];
        g_coverNext = (g_coverNext + 1) % (int)(sizeof(g_coverMemo) / sizeof(g_coverMemo[0]));
        m.key = key;
        m.gen = _gen;
        memcpy(m.path, found.c_str(), found.length() + 1);
    }
    return found;
}

