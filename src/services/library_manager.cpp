#include "library_manager.h"
#include "../core/progress_sync.h"
#include "sync_clock.h"
#include "../reader/reader.h"
#include "../convert/sd_convert.h"
#include "../reader/book_reader.h"
#include "../ui/jpeg_viewer.h"
#include "../net/bookorbit_client.h"
#include "../core/book_formats.h"
#include "../ui/i18n.h"
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>

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
static String epubCoverPath(const String& filePath) {
    String base = BookReader::cacheKeyFor(filePath);
    const int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    return "/covers/" + base + ".jpg";
}

static bool isEpubPath(const String& filePath) {
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
                "folder.jpg", "folder.jpeg"
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
            }
        }
    }

    // 2. /covers/<file name>.jpg: the server's cover of a downloaded book, or the
    //    one extracted from the EPUB (both by the file's own name, never by the
    //    title: two books of one title do not share a cover).
    if (filePath.length() > 0) {
        String cPath = epubCoverPath(filePath);
        if (isValidCoverFile(cPath)) return cPath;

        String cPathJpeg = cPath;
        if (cPathJpeg.endsWith(".jpg")) {
            cPathJpeg = cPathJpeg.substring(0, cPathJpeg.length() - 4) + ".jpeg";
            if (isValidCoverFile(cPathJpeg)) return cPathJpeg;
        }

        // 3. Extract embedded cover from EPUB if allowed. A cover that cannot be
        //    drawn (progressive JPEG, PNG) is not extracted again this session:
        //    the home screen asks for it on every redraw.
        static std::vector<String> s_unusableEpubCovers;
        bool knownBad = false;
        for (const auto& p : s_unusableEpubCovers) if (p == cPath) { knownBad = true; break; }
        // A converted book (FB2) has its EPUB in its cache directory once it was opened.
        String epub = filePath, indexDir;
        if (convert::needsConversion(filePath)) {
            indexDir = BookReader::cacheDirFor(filePath);
            epub = indexDir + "/book.epub";
        }
        if (allowExtract && !knownBad && (isEpubPath(filePath) || indexDir.length()) && Storage.exists(epub.c_str())) {
            bool noMemory = false;
            if (BookReader::extractCover(epub, cPath, &noMemory, indexDir) && isValidCoverFile(cPath)) return cPath;
            if (!noMemory) s_unusableEpubCovers.push_back(cPath);   // short of memory now: tried again later
        }
    }

    return "";
}

namespace {

String stemOf(const String& path) {
    const int slash = path.lastIndexOf('/');
    String base = slash >= 0 ? path.substring(slash + 1) : path;
    const int dot = base.lastIndexOf('.');
    return dot > 0 ? base.substring(0, dot) : base;
}

void removeIfExists(const String& path) {
    if (path.length() > 0 && Storage.exists(path.c_str())) {
        Storage.remove(path.c_str());
        Serial.printf("[Library]   removed %s\n", path.c_str());
    }
}

// A cover stem in /covers/: the JPEG and every BMP render JpegViewer caches
// for it (thumb/full, crop/fit).
void removeCoverStem(const String& stem) {
    if (stem.length() == 0) return;
    const char* suffixes[] = { ".jpg", ".jpeg", "_thumb_crop.bmp", "_thumb_fit.bmp", "_crop.bmp", "_fit.bmp" };
    for (const char* sfx : suffixes) removeIfExists("/covers/" + stem + sfx);
}

}  // namespace

void LibraryManager::deleteBookArtifacts(const BookItem& book, bool removeCover) {
    removeIfExists(book.filePath);
    // Legacy indices kept next to the book file.
    removeIfExists(book.filePath + ".idx");
    for (int f = 0; f < 4; f++) removeIfExists(book.filePath + "_f" + String(f) + ".idx");

    // The layout engine's cache directory (container index, page caches of every
    // font size and screen, so one SD card shared by several devices is cleaned too).
    const String fib = BookReader::cacheDirFor(book.filePath);
    if (Storage.exists(fib.c_str())) Storage.removeDir(fib.c_str());

    // Caches of the old text converter: /cache/<stem>.*. Matched by exact suffix
    // so that a book whose name starts the same ("Name" vs "Name (2020)") is left alone.
    const String stem = stemOf(book.filePath);
    if (stem.length() > 0) {
        const String txt = stem + ".txt";
        for (const String& n : Storage.listFileNames("/cache")) {
            const bool mine = n.startsWith(txt) ||                 // .txt, .txt.idx, .txt_f1[_WxH].idx, .txt.tmp
                              n == stem + ".toc" || n == stem + ".toc.tmp" ||
                              n == stem + ".spine" || n == stem + ".cfimap";
            if (mine) removeIfExists("/cache/" + n);
        }
    }

    if (removeCover) {
        removeCoverStem(stemOf(BookReader::cacheKeyFor(book.filePath)));   // extracted from the EPUB
        if (book.bookId > 0) removeCoverStem(catalogStem(book.title, book.bookId));  // downloaded from the server
        // An explicit cover in /covers/ (never a cover.jpg in the book's folder).
        if (book.coverPath.startsWith("/covers/")) removeCoverStem(stemOf(book.coverPath));
    }
    JpegViewer::clearCache();
}

namespace {

// Stems (file name without extension) of every book file under `dir`.
void collectBookStems(const String& dir, int depth, std::vector<String>& out, int& files,
                      std::vector<String>* names = nullptr) {
    if (depth > 4) return;
    for (const DirEntry& e : Storage.listDirectory(dir)) {  // books and folders only
        if (e.isDirectory) {
            collectBookStems(e.fullPath, depth + 1, out, files, names);
        } else {
            const String key = BookReader::cacheKeyFor(e.fullPath);   // how its cache and cover are named
            out.push_back(stemOf(key));
            if (names) names->push_back(key);
            files++;
        }
    }
}

bool contains(const std::vector<String>& v, const String& s) {
    for (const String& x : v) if (x == s) return true;
    return false;
}

// The cover stem a /covers file belongs to, "" if the name is not one we write.
String coverOwner(const String& n) {
    const char* sfx[] = { "_thumb_crop.bmp", "_thumb_fit.bmp", "_crop.bmp", "_fit.bmp", ".jpeg", ".jpg" };
    for (const char* x : sfx) {
        if (n.endsWith(x)) return n.substring(0, n.length() - strlen(x));
    }
    return "";
}

}  // namespace

void LibraryManager::cleanOrphanFiles() {
    if (!Storage.isAvailable()) return;
    const uint32_t t0 = millis();

    std::vector<String> books, bookNames;
    int bookFiles = 0;
    collectBookStems("/", 0, books, bookFiles, &bookNames);
    if (bookFiles == 0) {
        // An empty or unreadable card would make every cache file look orphaned.
        Serial.println("[Library] Cleanup skipped: no book files found.");
        return;
    }

    std::vector<String> covers = books;
    std::vector<BookMetadata> catalog;
    // An unreadable catalog would make the covers of every cloud book look orphaned.
    const bool catalogKnown = Storage.loadCatalog(catalog) || !Storage.exists("/books/catalog.json");
    for (const auto& b : catalog) covers.push_back(catalogStem(b.title, catalogBookId(b)));
    if (lastBook.coverPath.startsWith("/covers/")) covers.push_back(stemOf(lastBook.coverPath));
    for (const auto& b : masterBooks) {
        if (b.coverPath.startsWith("/covers/")) covers.push_back(stemOf(b.coverPath));
    }

    int removed = 0;
    // Plain files in /cache are the old text converter's (text, TOC, spine, page
    // indices): the layout engine keeps everything in per-book directories.
    for (const String& n : Storage.listFileNames("/cache")) {
        removeIfExists("/cache/" + n);
        removed++;
    }
    // "<cache key>.fib": the layout cache of a book that is no longer on the card.
    for (const String& d : Storage.listSubdirs("/cache")) {
        if (!d.endsWith(".fib")) continue;
        const String file = d.substring(0, d.length() - 4);
        if (!contains(bookNames, file)) {
            Storage.removeDir(("/cache/" + d).c_str());
            removed++;
        }
    }
    for (const String& n : catalogKnown ? Storage.listFileNames("/covers") : std::vector<String>()) {
        const String owner = coverOwner(n);
        if (owner.length() > 0 && !contains(covers, owner)) {
            removeIfExists("/covers/" + n);
            removed++;
        }
    }
    if (removed > 0) JpegViewer::clearCache();
    Serial.printf("[Library] Cleanup: %d book files, %d orphaned files removed in %lu ms\n", bookFiles, removed,
                  (unsigned long)(millis() - t0));
}

void LibraryManager::updateLastBookAfterDeletion(const String& deletedPath) {
    touch();
    if (!lastBook.filePath.equalsIgnoreCase(deletedPath)) return;
    // The current book left the card: the most recent other book on the card
    // becomes current (or none), so the home card does not show a deleted book.
    lastBook = LastBookInfo();
    std::vector<RecentBookInfo> recent = getRecentBooks();
    if (!recent.empty()) {
        const RecentBookInfo& r = recent[0];
        lastBook.filePath = r.filePath;
        lastBook.title = r.title;
        lastBook.author = r.author;
        lastBook.coverPath = r.coverPath;
        lastBook.fileId = r.fileId;
        lastBook.progress = r.progress;
        lastBook.page = r.page;
        lastBook.xpath = r.xpath;
        lastBook.totalPages = 1;
    }
    recentBookIndex = 0;
    Storage.saveLastBook(lastBook);
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

std::vector<RecentBookInfo> LibraryManager::getRecentBooks() {
    std::vector<RecentBookInfo> list;
    auto toInfo = [&](const BookItem& b) {
        RecentBookInfo r;
        r.title = b.title;
        r.author = b.author;
        r.filePath = b.filePath;
        r.coverPath = recentCover(b.filePath, b.title, b.coverPath);
        r.downloadUrl = b.downloadUrl();
        r.fileId = b.fileId;
        r.progress = b.progress;
        r.page = b.page;
        r.docId = b.docId;
        r.xpath = b.xpath;
        r.totalPages = 1;
        r.isDownloaded = true;
        return r;
    };
    auto listed = [&](const String& path) {
        for (const auto& e : list) if (e.filePath.equalsIgnoreCase(path)) return true;
        return false;
    };

    // 1. The current book, if it is still on the card.
    if (lastBook.filePath.length() > 0 && Storage.exists(lastBook.filePath.c_str())) {
        RecentBookInfo r;
        r.title = lastBook.title;
        r.author = lastBook.author;
        r.filePath = lastBook.filePath;
        r.coverPath = recentCover(r.filePath, r.title, lastBook.coverPath);
        r.fileId = lastBook.fileId;
        r.progress = lastBook.progress;
        r.page = lastBook.page;
        r.docId = lastBook.docId;
        r.xpath = lastBook.xpath;
        r.totalPages = max(1, lastBook.totalPages);
        r.isDownloaded = true;
        for (const auto& b : masterBooks) {
            if ((lastBook.fileId > 0 && b.fileId == lastBook.fileId) || b.filePath.equalsIgnoreCase(lastBook.filePath)) {
                if (b.fileId > 0 && r.fileId <= 0) { r.fileId = b.fileId; lastBook.fileId = b.fileId; }
                if (b.docId.length() > 0 && r.docId.length() == 0) { r.docId = b.docId; lastBook.docId = b.docId; }
                if (r.author.length() == 0) r.author = b.author;
                break;
            }
        }
        list.push_back(r);
    }

    // 2. Books on the card opened here (they have a position record), newest move first.
    ProgressStore& store = ProgressStore::instance();
    std::vector<std::pair<uint32_t, int>> opened;   // (last local move, master index)
    for (int i = 0; i < (int)masterBooks.size(); i++) {
        const BookItem& b = masterBooks[i];
        if (!b.isDownloaded) continue;
        if (const BookProgressRecord* rec = store.find(b.fileId, b.filePath)) {
            if (ProgressSync::hasProgress(rec->state.local.percent) || rec->state.local.page > 1) {
                opened.push_back({ rec->state.localChangedAt, i });
            }
        }
    }
    std::stable_sort(opened.begin(), opened.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& o : opened) {
        if (list.size() >= 4) break;
        const BookItem& b = masterBooks[o.second];
        if (!listed(b.filePath)) list.push_back(toInfo(b));
    }

    // 3. Fill up with other books on the card.
    for (const auto& b : masterBooks) {
        if (list.size() >= 4) break;
        if (!b.isDownloaded || b.isDirectory || listed(b.filePath)) continue;
        if (!b.formats.empty() && !BookFormats::readable((BookFormats::Kind)b.activeKind())) continue;   // not openable yet
        list.push_back(toInfo(b));
    }
    return list;
}

// ---------------------------------------------------------------- the book list
//
// masterBooks holds every book once: the files under /books (depth <= 3) merged
// with the saved server catalog. A screen's list (rows) is a vector of indices
// into it, plus folderRows for the rows that are not books of the master list
// (virtual folders such as an author, SD folders, "..", and files too deep for
// the master scan). So list entries are references: downloading, deleting or
// syncing a book from a list changes the master entry itself.

namespace {

bool isCompleted(const BookItem& b) { return b.progress >= LibraryManager::kCompletedAt; }

// Hidden by "hide completed": a completed book, but never the one being read.
bool hiddenAsCompleted(const BookItem& b, bool hide) {
    return hide && isCompleted(b) && !b.filePath.equalsIgnoreCase(LibraryManager::getInstance().lastBook.filePath);
}

// Folded copy for substring search (search runs once per query).
String foldString(const String& s) {
    String out;
    out.reserve(s.length());
    const char* p = s.c_str();
    char buf[4];
    while (*p) {
        const uint32_t cp = LibraryManager::foldCodepoint(LibraryManager::nextCodepoint(p));
        if (cp < 0x80) { out += (char)cp; continue; }
        int n = 0;
        if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); }
        else { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); }
        buf[n++] = (char)(0x80 | (cp & 0x3F));
        for (int i = 0; i < n; i++) out += buf[i];
    }
    return out;
}

String trimmed(const String& s) {
    String t = s;
    t.trim();
    return t;
}

}  // namespace

uint32_t LibraryManager::nextCodepoint(const char*& p) {
    const uint8_t c = (uint8_t)p[0];
    if (!c) return 0;
    if (c < 0x80) { p += 1; return c; }
    const uint8_t c1 = (uint8_t)p[1];
    if ((c & 0xE0) == 0xC0 && (c1 & 0xC0) == 0x80) { p += 2; return ((c & 0x1F) << 6) | (c1 & 0x3F); }
    const uint8_t c2 = c1 ? (uint8_t)p[2] : 0;
    if ((c & 0xF0) == 0xE0 && (c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
        p += 3;
        return ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    }
    p += 1;   // stray byte: compared as itself
    return c;
}

uint32_t LibraryManager::foldCodepoint(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;   // А..Я -> а..я
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;   // Ѐ..Џ (Ё) -> ѐ..џ (ё)
    return cp;
}

int LibraryManager::foldCompare(const String& a, const String& b) {
    const char* pa = a.c_str();
    const char* pb = b.c_str();
    while (true) {
        const uint32_t ca = foldCodepoint(nextCodepoint(pa));
        const uint32_t cb = foldCodepoint(nextCodepoint(pb));
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
}

bool LibraryManager::utf8ContainsIgnoreCase(const String& haystack, const String& needle) {
    if (needle.length() == 0) return true;
    return foldString(haystack).indexOf(foldString(needle)) >= 0;
}

int LibraryManager::rowCount() const { return (int)rows.size(); }

BookItem& LibraryManager::rowAt(int i) {
    const int r = rows[i];
    return r >= 0 ? masterBooks[r] : folderRows[-1 - r];
}

int LibraryManager::rowMasterIndex(int i) const {
    return (i >= 0 && i < (int)rows.size() && rows[i] >= 0) ? rows[i] : -1;
}

const String& LibraryManager::libraryOf(const BookItem& b) const {
    static const String empty;
    return (b.libraryIdx >= 0 && b.libraryIdx < (int)libraryNames.size()) ? libraryNames[b.libraryIdx] : empty;
}

int LibraryManager::internLibrary(const String& name) {
    const String n = trimmed(name);
    if (n.length() == 0) return -1;
    for (size_t i = 0; i < libraryNames.size(); i++) {
        if (foldCompare(libraryNames[i], n) == 0) return (int)i;
    }
    libraryNames.push_back(n);
    return (int)libraryNames.size() - 1;
}

void LibraryManager::setHideCompleted(bool hide) {
    if (hideCompleted == hide) return;
    hideCompleted = hide;
    _countsDirty = true;
}

// ---------------------------------------------------------------- formats

String BookItem::downloadUrlOf(const BookFormatSlot& f) const {
    if (f.url.length() > 0) return f.url;
    if (bookId <= 0 || f.fileId <= 0) return String();
    return String("/api/v1/opds/") + String(bookId) + "/download?fileId=" + String(f.fileId);
}

String BookItem::downloadUrl() const {
    if (activeFormat < 0 || activeFormat >= (int)formats.size()) return String();
    return downloadUrlOf(formats[activeFormat]);
}

bool BookItem::inCatalog() const {
    for (const auto& f : formats) if (f.fileId > 0 || f.url.length() > 0) return true;
    return false;
}

bool BookItem::anyOnCard() const {
    for (const auto& f : formats) if (f.onCard) return true;
    return isDownloaded;
}

namespace {

// "/books/Name.epub" -> "/books/Name" (the part shared by all formats of a book).
String withoutExtension(const String& path) {
    const int slash = path.lastIndexOf('/');
    const int dot = path.lastIndexOf('.');
    return dot > slash + 1 ? path.substring(0, dot) : path;
}

// Folded "<dir>/<stem>": formats of one book share it.
std::string bookKey(const String& path) { return std::string(foldString(withoutExtension(path)).c_str()); }

bool isBookFile(const String& name) {
    return BookFormats::fromPath(name.c_str()) != BookFormats::Unknown;
}

}  // namespace

String LibraryManager::formatPath(const BookItem& b, const BookFormatSlot& f) {
    // The path as found on the card keeps its spelling ("Name.EPUB").
    if (BookFormats::fromPath(b.filePath.c_str()) == (BookFormats::Kind)f.kind) return b.filePath;
    return withoutExtension(b.filePath) + BookFormats::extension((BookFormats::Kind)f.kind);
}

void LibraryManager::selectFormat(BookItem& b, int i) {
    if (i < 0 || i >= (int)b.formats.size()) return;
    const String path = formatPath(b, b.formats[i]);
    if (b.activeFormat != i) {
        Serial.printf("[Library] '%s': format %s (%s)\n", b.title.c_str(),
                      BookFormats::label((BookFormats::Kind)b.formats[i].kind), path.c_str());
    }
    b.activeFormat = (int8_t)i;
    b.filePath = path;
    b.fileId = b.formats[i].fileId;
    b.isDownloaded = b.formats[i].onCard;
}

int LibraryManager::lastReadFormat(const BookItem& b) {
    ProgressStore& store = ProgressStore::instance();
    int best = -1;
    uint32_t bestAt = 0;
    for (int i = 0; i < (int)b.formats.size(); i++) {
        const BookFormatSlot& f = b.formats[i];
        if (!f.onCard) continue;
        const BookProgressRecord* rec = store.find(f.fileId, formatPath(b, f));
        if (!rec) continue;
        const bool read = ProgressSync::hasProgress(rec->state.local.percent) || rec->state.local.page > 1;
        if (!read) continue;
        if (best < 0 || rec->state.localChangedAt > bestAt) { best = i; bestAt = rec->state.localChangedAt; }
    }
    // The book open last is the current one, whatever the change times say.
    const LastBookInfo& last = getInstance().lastBook;
    for (int i = 0; i < (int)b.formats.size(); i++) {
        if (b.formats[i].onCard && last.filePath.equalsIgnoreCase(formatPath(b, b.formats[i]))) return i;
    }
    return best;
}

void LibraryManager::chooseFormat(BookItem& b) {
    if (b.formats.empty()) return;
    int pick = lastReadFormat(b);
    auto best = [&](auto ok) {
        int idx = -1;
        for (int i = 0; i < (int)b.formats.size(); i++) {
            if (!ok(b.formats[i])) continue;
            if (idx < 0 || BookFormats::rank((BookFormats::Kind)b.formats[i].kind) <
                           BookFormats::rank((BookFormats::Kind)b.formats[idx].kind)) idx = i;
        }
        return idx;
    };
    auto readable = [](const BookFormatSlot& f) { return BookFormats::readable((BookFormats::Kind)f.kind); };
    if (pick < 0) pick = best([&](const BookFormatSlot& f) { return f.onCard && readable(f); });
    if (pick < 0) pick = best([&](const BookFormatSlot& f) { return readable(f) && (f.fileId > 0 || f.url.length() > 0); });
    if (pick < 0) pick = best([](const BookFormatSlot& f) { return f.onCard; });
    if (pick < 0) pick = 0;
    selectFormat(b, pick);
}

void LibraryManager::openFormats(int idx) {
    if (idx < 0 || idx >= (int)masterBooks.size()) return;
    if (!currentFolder.equals("@formats")) {
        formatsReturnFolder = currentFolder;
        formatsReturnIndex = selectedBookIndex;
    }
    formatsBook = idx;
    currentFolder = "@formats";
    Serial.printf("[Library] Formats of '%s': %u\n", masterBooks[idx].title.c_str(), (unsigned)masterBooks[idx].formats.size());
    // Start on the active format (row 0 is "..").
    selectedBookIndex = masterBooks[idx].activeFormat >= 0 ? masterBooks[idx].activeFormat + 1 : 1;
    rebuildRows();
}

void LibraryManager::closeFormats() {
    if (!currentFolder.equals("@formats")) return;
    currentFolder = formatsReturnFolder.length() > 0 ? formatsReturnFolder : String("/books");
    selectedBookIndex = formatsReturnIndex;
    formatsBook = -1;
    rebuildRows();
}

static void collectAllLocalBooks(const String& dir, int depth, std::vector<BookItem>& out, int* topFolders,
                                 std::unordered_map<std::string, int>* byKey = nullptr) {
    if (depth > 3) return;
    std::unordered_map<std::string, int> ownKeys;
    if (!byKey) byKey = &ownKeys;
    for (const DirEntry& entry : Storage.listDirectory(dir)) {   // folders and book files only
        if (entry.name.startsWith(".")) continue;
        if (entry.isDirectory) {
            if (depth == 0 && topFolders) (*topFolders)++;
            collectAllLocalBooks(entry.fullPath, depth + 1, out, nullptr, byKey);
            continue;
        }
        const BookFormats::Kind kind = BookFormats::fromPath(entry.name.c_str());
        if (kind == BookFormats::Unknown) continue;

        BookFormatSlot slot;
        slot.kind = (uint8_t)kind;
        slot.onCard = true;
        // "Name.epub" and "Name.fb2" in one folder are one book in two formats.
        const std::string key = bookKey(entry.fullPath);
        auto it = byKey->find(key);
        if (it != byKey->end()) {
            out[it->second].formats.push_back(slot);
            continue;
        }

        BookItem item;
        const int dot = entry.name.lastIndexOf('.');
        item.title = (dot > 0) ? entry.name.substring(0, dot) : entry.name;
        item.filePath = entry.fullPath;
        item.coverPath = epubCoverPath(entry.fullPath);
        item.isDownloaded = true;
        item.formats.push_back(slot);
        item.activeFormat = 0;
        const LastBookInfo& last = LibraryManager::getInstance().lastBook;
        if (withoutExtension(last.filePath).equalsIgnoreCase(withoutExtension(entry.fullPath)) && last.author.length() > 0)
            item.author = last.author;
        byKey->emplace(key, (int)out.size());
        out.push_back(item);
    }
}

void LibraryManager::buildMaster() {
    touch();
    const uint32_t t0 = millis();
    masterBooks.clear();
    libraryNames.clear();
    int folders = 0;
    std::vector<BookMetadata> catalog;
    Storage.loadCatalog(catalog);
    // One allocation for the list (it lives until the next sync), made before the
    // per-book strings so it does not end up between them.
    masterBooks.reserve(catalog.size() + 16);
    collectAllLocalBooks("/books", 0, masterBooks, &folders);
    counts.folders = folders;
    migrateLegacyNames(catalog);

    // Lookup tables for merging the catalog (was a pairwise scan of both lists).
    // A local book is found by a server file id or by its path without extension
    // (any of its formats). Not by its title: two books of one title are two books.
    std::unordered_map<std::string, int> byKey, byFileId;
    for (int i = 0; i < (int)masterBooks.size(); i++) byKey.emplace(bookKey(masterBooks[i].filePath), i);

    for (auto& b : catalog) {
        const String destPath = catalogBookPath(b);
        const int bookId = catalogBookId(b);
        int idx = -1;
        for (const auto& f : b.formats) {
            if (idx >= 0 || f.fileId <= 0) continue;
            auto it = byFileId.find(std::to_string(f.fileId));
            if (it != byFileId.end()) idx = it->second;
        }
        if (idx < 0 && b.fileId > 0) {
            auto it = byFileId.find(std::to_string(b.fileId));
            if (it != byFileId.end()) idx = it->second;
        }
        if (idx < 0) { auto it = byKey.find(bookKey(destPath)); if (it != byKey.end()) idx = it->second; }
        if (idx < 0) {
            masterBooks.push_back(BookItem());
            idx = (int)masterBooks.size() - 1;
            BookItem& n = masterBooks.back();
            n.title = b.title;
            n.filePath = destPath;
            n.isDownloaded = false;   // a downloaded copy would have matched a file of the scan
            byKey.emplace(bookKey(destPath), idx);
        }
        BookItem& item = masterBooks[idx];
        // The server's title, not the file's name ("<title> [<id>]").
        item.title = b.title;
        // The server formats join the files found on the card (same extension).
        for (auto& f : b.formats) {
            BookFormatSlot* slot = nullptr;
            for (auto& sl : item.formats) {
                if ((f.fileId > 0 && sl.fileId == f.fileId) || (sl.fileId <= 0 && sl.kind == f.kind)) { slot = &sl; break; }
            }
            if (!slot) {
                item.formats.push_back(BookFormatSlot());
                slot = &item.formats.back();
                slot->kind = f.kind;
            }
            slot->fileId = f.fileId;
            slot->url = std::move(f.url);   // the catalog copy is dropped below
            if (f.fileId > 0) byFileId.emplace(std::to_string(f.fileId), idx);
        }
        // The cover is resolved when it is drawn (home card); the server one lands here.
        if (!item.isDownloaded || item.coverPath.length() == 0) item.coverPath = "/covers/" + catalogStem(b.title, bookId) + ".jpg";
        item.coverUrl = std::move(b.coverUrl);
        if (bookId > 0) item.bookId = bookId;
        if (item.author.length() == 0) item.author = std::move(b.author);
        if (b.series.length() > 0) { item.series = std::move(b.series); item.seriesIndex = b.seriesIndex; }
        if (b.library.length() > 0) item.libraryIdx = (int16_t)internLibrary(b.library);
        // No position from the catalog: this device's own (if any) comes in refreshProgress().
    }
    const size_t catalogSize = catalog.size();
    std::vector<BookMetadata>().swap(catalog);   // before the per-book work below
    for (auto& item : masterBooks) chooseFormat(item);
    refreshProgress();
    for (const auto& item : masterBooks) {
        if (lastBook.fileId <= 0 && item.fileId > 0 && lastBook.filePath.equalsIgnoreCase(item.filePath)) {
            lastBook.fileId = item.fileId;
        }
    }
    Serial.printf("[Library] Master list: %d books (%d in the catalog, %d libraries) in %lu ms\n",
                  (int)masterBooks.size(), (int)catalogSize, (int)libraryNames.size(),
                  (unsigned long)(millis() - t0));
}

void LibraryManager::refreshProgressOf(BookItem& item) {
    if (const BookProgressRecord* rec = ProgressStore::instance().find(item.fileId, item.filePath)) {
        item.progress = rec->state.local.percent;
        item.page = rec->state.local.page > 0 ? rec->state.local.page : 1;
        item.xpath = rec->state.local.xpath.c_str();
        if (item.fileId <= 0 && rec->fileId > 0) item.fileId = rec->fileId;
        if (item.bookId <= 0 && rec->bookId > 0) item.bookId = rec->bookId;
    } else if (item.formats.size() > 1) {
        // A format never opened here starts at the beginning (formats are read separately).
        item.progress = 0.0f;
        item.page = 1;
        item.xpath = "";
    }
}

void LibraryManager::refreshProgress() {
    ProgressStore& store = ProgressStore::instance();
    for (auto& item : masterBooks) {
        if (store.find(item.fileId, item.filePath)) refreshProgressOf(item);
    }
}

void LibraryManager::refreshLocalFiles() {
    touch();
    if (!Storage.isAvailable()) return;
    std::vector<BookItem> found;
    int folders = 0;
    collectAllLocalBooks("/books", 0, found, &folders);
    counts.folders = folders;

    // Every file on the card, by path, and the books found, by path without extension.
    std::unordered_map<std::string, int> foundByPath, foundByKey;
    for (int i = 0; i < (int)found.size(); i++) {
        foundByKey.emplace(bookKey(found[i].filePath), i);
        for (const auto& f : found[i].formats) foundByPath.emplace(foldString(formatPath(found[i], f)).c_str(), i);
    }
    std::vector<bool> matched(found.size(), false);

    int added = 0, gone = 0;
    for (size_t i = 0; i < masterBooks.size();) {
        BookItem& b = masterBooks[i];
        bool changed = false;
        for (auto& f : b.formats) {
            auto it = foundByPath.find(foldString(formatPath(b, f)).c_str());
            const bool onCard = it != foundByPath.end();
            if (onCard) matched[it->second] = true;
            if (onCard != f.onCard) { f.onCard = onCard; changed = true; }
        }
        if (b.formats.empty()) {   // not expected for a book, kept as before
            auto it = foundByPath.find(foldString(b.filePath).c_str());
            const bool onCard = it != foundByPath.end();
            if (onCard) matched[it->second] = true;
            if (onCard != b.isDownloaded) { b.isDownloaded = onCard; changed = true; }
        }
        // A new format of a book already in the list ("Name.fb2" next to "Name.epub").
        auto kt = foundByKey.find(bookKey(b.filePath));
        if (kt != foundByKey.end()) {
            for (const auto& nf : found[kt->second].formats) {
                bool known = false;
                for (const auto& f : b.formats) known = known || f.kind == nf.kind;
                if (!known) { b.formats.push_back(nf); changed = true; }
            }
            matched[kt->second] = true;
        }
        if (changed) {
            bool anyCard = false;
            for (const auto& f : b.formats) anyCard = anyCard || f.onCard;
            if (!anyCard && !b.inCatalog() && !b.formats.empty()) {   // a local-only book removed from the card
                masterBooks.erase(masterBooks.begin() + i);
                gone++;
                continue;
            }
            const int8_t keep = b.activeFormat;
            if (keep >= 0 && keep < (int)b.formats.size() && b.formats[keep].onCard) selectFormat(b, keep);
            else chooseFormat(b);
        }
        i++;
    }
    for (size_t i = 0; i < found.size(); i++) {
        if (!matched[i]) { masterBooks.push_back(found[i]); chooseFormat(masterBooks.back()); added++; }
    }
    if (added) refreshProgress();
    _countsDirty = true;
    if (added || gone) Serial.printf("[Library] Card changed: %d new books, %d removed\n", added, gone);
}

void LibraryManager::refreshCounts() {
    _countsDirty = false;
    Counts c;
    c.folders = counts.folders;
    std::vector<int> visible;
    visible.reserve(masterBooks.size());
    for (int i = 0; i < (int)masterBooks.size(); i++) {
        const BookItem& b = masterBooks[i];
        if (isCompleted(b)) c.completed++;
        if (hiddenAsCompleted(b, hideCompleted)) continue;
        visible.push_back(i);
        c.total++;
        if (b.anyOnCard()) c.downloaded++;
    }
    // Distinct authors / series: sort by the folded name, count the changes.
    auto distinct = [&](auto field, bool skipEmpty) {
        std::vector<int> v;
        for (int i : visible) if (!skipEmpty || field(masterBooks[i]).length() > 0) v.push_back(i);
        std::sort(v.begin(), v.end(), [&](int a, int b) { return foldCompare(field(masterBooks[a]), field(masterBooks[b])) < 0; });
        int n = 0;
        for (size_t k = 0; k < v.size(); k++) {
            if (k == 0 || foldCompare(field(masterBooks[v[k]]), field(masterBooks[v[k - 1]])) != 0) n++;
        }
        return n;
    };
    c.authors = distinct([](const BookItem& b) -> const String& { return b.author; }, false);
    c.series = distinct([](const BookItem& b) -> const String& { return b.series; }, true);
    std::vector<bool> libSeen(libraryNames.size(), false);
    bool unassigned = false;
    for (int i : visible) {
        const int l = masterBooks[i].libraryIdx;
        if (l >= 0) libSeen[l] = true; else unassigned = true;
    }
    for (bool s : libSeen) c.libraries += s ? 1 : 0;
    if (unassigned && c.libraries > 0) c.libraries++;   // the "[no library]" group
    counts = c;
}

void LibraryManager::rebuildFilteredIndices() {
    rebuildRows();
    _countsDirty = true;
}

void LibraryManager::scanLocalBooks(bool forceMasterScan) {
    if (!Storage.isAvailable()) {
        Serial.println("[Library] MicroSD storage not available.");
        rows.clear();
        folderRows.clear();
        return;
    }
    if (currentFolder.length() == 0) currentFolder = "/books";
    if (masterBooks.empty() || forceMasterScan || !_scanned) buildMaster();
    else refreshProgress();   // positions changed by reading / syncing since the last list
    _scanned = true;
    rebuildRows();
    _countsDirty = true;
}

void LibraryManager::rebuildRows() {
    touch();
    rows.clear();
    folderRows.clear();
    auto addFolder = [&](const BookItem& f) {
        folderRows.push_back(f);
        rows.push_back(-(int)folderRows.size());
    };
    auto folderItem = [](const String& title, const String& path, int count) {
        BookItem it;
        it.title = title;
        it.filePath = path;
        it.isDirectory = true;
        it.isDownloaded = true;
        it.subItemCount = count;
        it.author = String(count);
        return it;
    };
    auto upItem = [&](const String& path, const String& label) {
        BookItem it = folderItem(".. (" + String(tr(MsgId::BOOK_MENU_BACK)) + ")", path, 0);
        it.author = label;
        addFolder(it);
    };
    auto visible = [&](const BookItem& b) { return !hiddenAsCompleted(b, hideCompleted); };
    auto byTitle = [&](int a, int b) { return foldCompare(masterBooks[a].title, masterBooks[b].title) < 0; };

    // Groups of the visible books by a name (author, series, library), sorted.
    auto groups = [&](auto name, const String& prefix, const char* emptyLabel, const char* emptyKey) {
        std::vector<int> v;
        for (int i = 0; i < (int)masterBooks.size(); i++) if (visible(masterBooks[i])) v.push_back(i);
        std::sort(v.begin(), v.end(), [&](int a, int b) { return foldCompare(name(masterBooks[a]), name(masterBooks[b])) < 0; });
        int emptyCount = 0;
        for (size_t k = 0; k < v.size();) {
            const String g = trimmed(name(masterBooks[v[k]]));
            size_t e = k + 1;
            while (e < v.size() && foldCompare(trimmed(name(masterBooks[v[e]])), g) == 0) e++;
            if (g.length() == 0) emptyCount += (int)(e - k);
            else addFolder(folderItem(g, prefix + g, (int)(e - k)));
            k = e;
        }
        if (emptyCount > 0 && emptyLabel) addFolder(folderItem(emptyLabel, prefix + emptyKey, emptyCount));
    };
    // Books of one group, sorted with `less`.
    auto members = [&](auto match, auto less) {
        std::vector<int> v;
        for (int i = 0; i < (int)masterBooks.size(); i++) {
            if (visible(masterBooks[i]) && match(masterBooks[i])) v.push_back(i);
        }
        std::sort(v.begin(), v.end(), less);
        for (int i : v) rows.push_back(i);
    };
    auto bySeriesThenTitle = [&](int a, int b) {
        const BookItem& x = masterBooks[a];
        const BookItem& y = masterBooks[b];
        if (x.seriesIndex != y.seriesIndex) return x.seriesIndex < y.seriesIndex;
        return foldCompare(x.title, y.title) < 0;
    };

    const String& f = currentFolder;
    if (f.equals("@formats")) {
        // The formats of one book: a row each, with its own place on the card and position.
        if (formatsBook < 0 || formatsBook >= (int)masterBooks.size()) {
            currentFolder = formatsReturnFolder.length() > 0 ? formatsReturnFolder : String("/books");
            formatsBook = -1;
            rebuildRows();
            return;
        }
        const BookItem& book = masterBooks[formatsBook];
        BookItem up = folderItem(".. (" + String(tr(MsgId::BOOK_MENU_BACK)) + ")", "@formats-up", 0);
        up.author = book.title;
        addFolder(up);
        ProgressStore& store = ProgressStore::instance();
        for (int i = 0; i < (int)book.formats.size(); i++) {
            const BookFormatSlot& slot = book.formats[i];
            BookItem it;
            it.title = BookFormats::label((BookFormats::Kind)slot.kind);
            it.author = book.author;
            it.filePath = formatPath(book, slot);
            it.fileId = slot.fileId;
            it.bookId = book.bookId;
            it.isDownloaded = slot.onCard;
            it.formats.push_back(slot);
            it.activeFormat = 0;
            it.formatRow = (int8_t)i;
            if (const BookProgressRecord* rec = store.find(slot.fileId, it.filePath)) {
                it.progress = rec->state.local.percent;
                it.page = rec->state.local.page > 0 ? rec->state.local.page : 1;
            } else if (slot.fileId > 0 && slot.fileId == book.fileId) {
                it.progress = book.progress;   // the server's position of this format
            }
            addFolder(it);
        }
    } else if (f.equalsIgnoreCase("@libraries")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        // Only a mix of library / no library gets the "[no library]" group.
        bool anyLib = false;
        for (const auto& b : masterBooks) if (b.libraryIdx >= 0 && visible(b)) { anyLib = true; break; }
        groups([this](const BookItem& b) -> const String& { return libraryOf(b); }, "@library:",
               anyLib ? tr(MsgId::CATALOG_UNKNOWN_LIBRARY) : nullptr, "__none__");
    } else if (f.startsWith("@library:")) {
        upItem("@libraries", tr(MsgId::CATALOG_BY_LIBRARIES));
        const String target = f.substring(9);
        const bool none = target == "__none__";
        members([&](const BookItem& b) {
            const String& l = libraryOf(b);
            return none ? l.length() == 0 : foldCompare(l, target) == 0;
        }, byTitle);
    } else if (f.equalsIgnoreCase("@authors")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        groups([](const BookItem& b) -> const String& { return b.author; }, "@author:",
               tr(MsgId::CATALOG_UNKNOWN_AUTHOR), "");
    } else if (f.startsWith("@author:")) {
        upItem("@authors", tr(MsgId::CATALOG_BY_AUTHORS));
        const String target = f.substring(8);
        members([&](const BookItem& b) { return foldCompare(trimmed(b.author), target) == 0; },
                [&](int a, int b) {
                    const BookItem& x = masterBooks[a];
                    const BookItem& y = masterBooks[b];
                    // Series first (alphabetically, in order), then standalone books.
                    if (foldCompare(x.series, y.series) != 0) {
                        if (x.series.length() == 0) return false;
                        if (y.series.length() == 0) return true;
                        return foldCompare(x.series, y.series) < 0;
                    }
                    return bySeriesThenTitle(a, b);
                });
    } else if (f.equalsIgnoreCase("@series")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        groups([](const BookItem& b) -> const String& { return b.series; }, "@series:",
               tr(MsgId::CATALOG_STANDALONE), "__none__");
    } else if (f.startsWith("@series:")) {
        upItem("@series", tr(MsgId::CATALOG_BY_SERIES));
        const String target = f.substring(8);
        const bool none = target == "__none__";
        members([&](const BookItem& b) {
            return none ? trimmed(b.series).length() == 0 : foldCompare(trimmed(b.series), target) == 0;
        }, none ? std::function<bool(int, int)>(byTitle) : std::function<bool(int, int)>(bySeriesThenTitle));
    } else if (currentFilter == FILTER_SEARCH || f.equalsIgnoreCase("@search")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        const String needle = foldString(searchQuery);
        members([&](const BookItem& b) {
            return needle.length() == 0 || foldString(b.title).indexOf(needle) >= 0 ||
                   foldString(b.author).indexOf(needle) >= 0 || foldString(b.series).indexOf(needle) >= 0;
        }, byTitle);
    } else if (f.startsWith("/books") && (currentFilter == FILTER_FOLDERS || f.length() > 6)) {
        // SD folders: the top level lists the folders of /books, a folder its
        // subfolders and book files.
        const bool top = f.equalsIgnoreCase("/books");
        if (top) {
            upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        } else {
            const int slash = f.lastIndexOf('/');
            const String parent = f.substring(0, slash);
            const bool parentIsTop = parent.equalsIgnoreCase("/books");
            upItem(parentIsTop && currentFilter != FILTER_FOLDERS ? String("/books") : parent,
                   parentIsTop ? String(tr(currentFilter == FILTER_FOLDERS ? MsgId::CATALOG_BY_FOLDERS : MsgId::CATALOG_ALL_BOOKS))
                               : parent.substring(parent.lastIndexOf('/') + 1));
        }
        std::vector<BookItem> dirs;
        std::vector<int> files;
        std::vector<BookItem> looseFiles;   // deeper than the master scan
        std::unordered_map<std::string, int> byKey;   // first book of each key, as the old scan found
        for (const DirEntry& e : Storage.listDirectory(f)) {
            if (e.name.startsWith(".")) continue;
            if (e.isDirectory) {
                const String prefix = e.fullPath + "/";
                int cnt = 0;
                for (const auto& mb : masterBooks) if (mb.isDownloaded && mb.filePath.startsWith(prefix)) cnt++;
                dirs.push_back(folderItem(e.name, e.fullPath, cnt));
                continue;
            }
            if (top) continue;
            if (!isBookFile(e.name)) continue;
            // The formats of a book ("Name.epub", "Name.fb2") are one row.
            if (byKey.empty()) {   // built once per list (was a scan of every book per file)
                for (int i = 0; i < (int)masterBooks.size(); i++) byKey.emplace(bookKey(masterBooks[i].filePath), i);
            }
            const auto found = byKey.find(bookKey(e.fullPath));
            const int idx = found != byKey.end() ? found->second : -1;
            if (idx >= 0) {
                if (visible(masterBooks[idx]) && std::find(files.begin(), files.end(), idx) == files.end()) files.push_back(idx);
            } else {
                BookItem it;
                const int dot = e.name.lastIndexOf('.');
                it.title = dot > 0 ? e.name.substring(0, dot) : e.name;
                it.filePath = e.fullPath;
                it.isDownloaded = true;
                BookFormatSlot slot;
                slot.kind = (uint8_t)BookFormats::fromPath(e.name.c_str());
                slot.onCard = true;
                it.formats.push_back(slot);
                it.activeFormat = 0;
                looseFiles.push_back(it);
            }
        }
        std::sort(dirs.begin(), dirs.end(), [](const BookItem& a, const BookItem& b) { return foldCompare(a.title, b.title) < 0; });
        std::sort(files.begin(), files.end(), byTitle);
        for (const auto& d : dirs) addFolder(d);
        for (int i : files) rows.push_back(i);
        for (const auto& it : looseFiles) addFolder(it);
    } else {
        // "/books": all books, downloaded or completed: the ones on the card, then
        // the ones only on the server, each by title (was the order of the card
        // scan, then the catalog's).
        for (int i = 0; i < (int)masterBooks.size(); i++) {
            const BookItem& b = masterBooks[i];
            if (currentFilter == FILTER_DOWNLOADED && !b.anyOnCard()) continue;
            if (currentFilter == FILTER_COMPLETED ? !isCompleted(b) : !visible(b)) continue;
            rows.push_back(i);
        }
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            const bool ca = masterBooks[a].anyOnCard(), cb = masterBooks[b].anyOnCard();
            return ca != cb ? ca : byTitle(a, b);
        });
    }

    if (selectedBookIndex >= (int)rows.size()) selectedBookIndex = rows.empty() ? 0 : (int)rows.size() - 1;
    if (selectedBookIndex < 0) selectedBookIndex = 0;
    Serial.printf("[Library] List %s: %d rows\n", currentFolder.c_str(), (int)rows.size());
}

void LibraryManager::initLastBook() {
    if (Storage.loadLastBook(lastBook)) {
        ProgressStore& store = ProgressStore::instance();
        if (BookProgressRecord* rec = store.find(lastBook.fileId, lastBook.filePath)) {
            // progress.json is the source of truth for the position.
            applyRecordToLibrary(*rec);
        } else if (ProgressSync::hasProgress(lastBook.progress)) {
            // Migration from firmware without progress.json: keep the old position
            // as "local, never synced" (no base). The next sync pulls the server
            // position if it has one, otherwise uploads this one.
            BookProgressRecord& r = store.getOrCreate(lastBook.fileId, 0, lastBook.filePath, lastBook.title);
            r.state.local.percent = lastBook.progress;
            r.state.local.page = lastBook.page;
            r.state.local.xpath = lastBook.xpath.c_str();
            store.save();
        }
        // Resolve fileId if missing in lastBook
        if (lastBook.fileId <= 0) {
            for (const auto& item : masterBooks) {
                if (item.filePath.equalsIgnoreCase(lastBook.filePath)) {
                    if (item.fileId > 0) {
                        lastBook.fileId = item.fileId;
                        saveLastBook();
                        break;
                    }
                }
            }
        }
    } else if (!masterBooks.empty()) {
        // Fallback to first book in library
        const BookItem& b = masterBooks[0];
        lastBook.filePath = b.filePath;
        lastBook.title = b.title;
        lastBook.author = b.author;
        lastBook.coverPath = b.coverPath;
        lastBook.fileId = b.fileId;
        lastBook.progress = b.progress;
        lastBook.page = b.page;
        lastBook.totalPages = 1;
        saveLastBook();
    }
}


bool LibraryManager::saveLastBook() {
    touch();
    _unsavedMoves = 0;
    bool ok = Storage.saveLastBook(lastBook);
    ProgressStore::instance().save();
    return ok;
}

// ---------------------------------------------------------------- progress

namespace {
String replaceChars(const String& title, const char* bad, const String& fallbackId) {
    String safe = title;
    for (const char* c = bad; *c; c++) safe.replace(String(*c), "_");
    safe.trim();
    if (safe.length() == 0) safe = "book_" + fallbackId;
    return safe;
}
// How firmware before November 2026 named a server book: its title alone, with
// fewer characters replaced (a "<" or "|" in a title made the download fail).
String legacyStem(const BookMetadata& b) { return replaceChars(b.title, "/\\:*?\"", b.id); }
}  // namespace

String LibraryManager::safeFileName(const String& title, const String& fallbackId) {
    return replaceChars(title, "/\\:*?\"<>|", fallbackId);
}

String LibraryManager::catalogStem(const String& title, int bookId) {
    const String safe = safeFileName(title, String(bookId));
    return bookId > 0 ? safe + " [" + String(bookId) + "]" : safe;
}

String LibraryManager::catalogFormatPath(const BookMetadata& b, const BookFormatEntry& f) {
    const char* ext = BookFormats::extension((BookFormats::Kind)f.kind);
    return "/books/" + catalogStem(b.title, catalogBookId(b)) + (*ext ? ext : ".epub");
}

String LibraryManager::catalogBookPath(const BookMetadata& b) {
    const BookFormatEntry* f = b.preferredFormat();
    if (f) return catalogFormatPath(b, *f);
    return "/books/" + catalogStem(b.title, catalogBookId(b)) + ".epub";
}

namespace {

bool renameIfThere(const String& from, const String& to) {
    if (from == to || !Storage.exists(from.c_str()) || Storage.exists(to.c_str())) return false;
    const bool ok = Storage.rename(from.c_str(), to.c_str());
    Serial.printf("[Library]   %s -> %s%s\n", from.c_str(), to.c_str(), ok ? "" : " FAILED");
    return ok;
}

// A cover stem in /covers: the JPEG and the BMP renders JpegViewer caches for it.
void renameCoverStem(const String& from, const String& to) {
    const char* suffixes[] = { ".jpg", ".jpeg", "_thumb_crop.bmp", "_thumb_fit.bmp", "_crop.bmp", "_fit.bmp" };
    for (const char* sfx : suffixes) renameIfThere("/covers/" + from + sfx, "/covers/" + to + sfx);
}

}  // namespace

void LibraryManager::migrateLegacyNames(const std::vector<BookMetadata>& catalog) {
    if (catalog.empty() || masterBooks.empty()) return;
    std::unordered_map<std::string, int> local;   // books on the card by folded "<dir>/<stem>"
    for (int i = 0; i < (int)masterBooks.size(); i++) local.emplace(bookKey(masterBooks[i].filePath), i);

    ProgressStore& store = ProgressStore::instance();
    auto hasRecord = [&](const BookMetadata& b) {
        for (const auto& f : b.formats) if (f.fileId > 0 && store.find(f.fileId, "")) return true;
        return false;
    };
    // The old name of each server book. Two books of one title shared it: the
    // file goes to the one with a position here, else to the first.
    std::unordered_map<std::string, int> owner;   // old key -> catalog index
    for (int c = 0; c < (int)catalog.size(); c++) {
        const BookMetadata& b = catalog[c];
        if (LibraryManager::catalogBookId(b) <= 0) continue;   // no id: the name has not changed
        const std::string oldKey = bookKey("/books/" + legacyStem(b) + ".epub");
        if (local.find(oldKey) == local.end()) continue;
        if (local.find(bookKey(catalogBookPath(b))) != local.end()) continue;   // already under its new name
        auto it = owner.find(oldKey);
        if (it == owner.end()) owner.emplace(oldKey, c);
        else if (!hasRecord(catalog[it->second]) && hasRecord(b)) it->second = c;
    }
    if (owner.empty()) return;

    const uint32_t t0 = millis();
    int moved = 0;
    // The last book as on the card: at boot the list is built before it is loaded.
    LastBookInfo saved;
    const bool haveSaved = Storage.loadLastBook(saved);
    bool savedChanged = false;
    for (const auto& o : owner) {
        const BookMetadata& b = catalog[o.second];
        BookItem& item = masterBooks[local[o.first]];
        // The book open in the reader keeps its name (its cache directory is in
        // use): it is renamed when the list is built again, at the next start or sync.
        if (Reader.isOpened() && withoutExtension(Reader.getOriginalFilePath()).equalsIgnoreCase(withoutExtension(item.filePath)))
            continue;
        const String oldStem = legacyStem(b);
        const String newStem = catalogStem(b.title, catalogBookId(b));
        Serial.printf("[Library] Renaming '%s' to its server id: %s\n", b.title.c_str(), newStem.c_str());
        for (auto& slot : item.formats) {
            if (!slot.onCard) continue;
            const String from = formatPath(item, slot);
            const String to = "/books/" + newStem + BookFormats::extension((BookFormats::Kind)slot.kind);
            if (!renameIfThere(from, to)) continue;
            // What goes by the book's file name: its layout cache and its bookmarks.
            renameIfThere(BookReader::cacheDirFor(from), BookReader::cacheDirFor(to));
            renameIfThere("/bookmarks/" + BookReader::cacheKeyFor(from) + ".json",
                          "/bookmarks/" + BookReader::cacheKeyFor(to) + ".json");
            // Position records under the old name: this book's go with it; another
            // book's (the same title, a file id of its own) to that book's name.
            for (auto& r : store.records()) {
                if (!r.filePath.equalsIgnoreCase(from)) continue;
                String path = to;
                if (r.fileId > 0) {
                    bool mine = false;
                    for (const auto& f : b.formats) mine = mine || f.fileId == r.fileId;
                    if (!mine) {
                        for (const auto& other : catalog) {
                            for (const auto& f : other.formats) {
                                if (f.fileId == r.fileId) path = catalogFormatPath(other, f);
                            }
                        }
                    }
                }
                r.filePath = path;
                store.touch();
            }
            if (lastBook.filePath.equalsIgnoreCase(from)) lastBook.filePath = to;
            if (haveSaved && saved.filePath.equalsIgnoreCase(from)) { saved.filePath = to; savedChanged = true; }
            moved++;
        }
        renameCoverStem(oldStem, newStem);
        const String oldCover = "/covers/" + oldStem + ".", newCover = "/covers/" + newStem + ".jpg";
        if (lastBook.coverPath.startsWith(oldCover)) lastBook.coverPath = newCover;
        if (haveSaved && saved.coverPath.startsWith(oldCover)) { saved.coverPath = newCover; savedChanged = true; }
        // The list entry follows the files.
        const int8_t active = item.activeFormat;
        item.filePath = "/books/" + newStem + BookFormats::extension((BookFormats::Kind)item.activeKind());
        item.coverPath = epubCoverPath(item.filePath);
        if (active >= 0) selectFormat(item, active);
    }
    if (moved > 0) {
        if (savedChanged) Storage.saveLastBook(saved);
        ProgressStore::instance().save();
        JpegViewer::clearCache();
        touch();
    }
    Serial.printf("[Library] %d files renamed to server ids in %lu ms\n", moved, (unsigned long)(millis() - t0));
}

int LibraryManager::catalogBookId(const BookMetadata& b) {
    int id = b.id.toInt();
    return id > 0 ? id : b.fileId;
}

BookProgressRecord& LibraryManager::currentRecord() {
    int bookId = 0;
    for (const auto& b : masterBooks) {
        if (b.filePath.equalsIgnoreCase(lastBook.filePath)) { bookId = b.bookId; break; }
    }
    return ProgressStore::instance().getOrCreate(lastBook.fileId, bookId, lastBook.filePath, lastBook.title);
}

// withCfi unzips the current chapter (~0.2-0.5 s): only when saving for sync.
static ProgressSync::Position readerPosition(bool withXPath, bool withCfi = false) {
    ProgressSync::Position pos;
    pos.percent = Reader.getProgress();
    pos.page = Reader.getCurrentPage();
    if (withXPath) pos.xpath = Reader.getXPath().c_str();
    if (withCfi) pos.cfi = Reader.getCfi().c_str();
    return pos;
}

void LibraryManager::recordReaderMove(bool withXPath, bool persist) {
    touch();
    if (!Reader.isOpened()) return;
    ProgressSync::Position pos = readerPosition(withXPath);
    lastBook.progress = pos.percent;
    lastBook.page = pos.page;
    lastBook.totalPages = Reader.getTotalPages();
    lastBook.xpath = pos.xpath.c_str();

    BookProgressRecord& rec = currentRecord();
    ProgressSync::recordLocalMove(rec.state, pos, SyncClock::nowUtc());
    ProgressStore::instance().touch();
    if (persist) {
        saveLastBook();
    } else if (_unsavedMoves++ == 0) {
        _firstUnsavedAt = millis();
    }
}

bool LibraryManager::persistIfDue() {
    constexpr int kMaxUnsavedPages = 5;
    constexpr uint32_t kMaxUnsavedMs = 60000;
    if (_unsavedMoves == 0) return false;
    if (_unsavedMoves < kMaxUnsavedPages && millis() - _firstUnsavedAt < kMaxUnsavedMs) return false;
    const uint32_t t0 = millis();
    const int pages = _unsavedMoves;
    saveLastBook();
    Serial.printf("[Library] Position saved while idle (%d page turns) in %lu ms\n", pages, (unsigned long)(millis() - t0));
    return true;
}

void LibraryManager::flushReaderPosition() {
    touch();
    if (!Reader.isOpened()) return;
    ProgressSync::Position pos = readerPosition(true, true);
    lastBook.progress = pos.percent;
    lastBook.page = pos.page;
    lastBook.totalPages = Reader.getTotalPages();
    lastBook.xpath = pos.xpath.c_str();

    // Not a user move: never touches the dirty flag or the change time.
    BookProgressRecord& rec = currentRecord();
    if (ProgressSync::samePlace(rec.state.local, pos)) {
        // Keep the percentage as recorded (it may be the exact server value after
        // a pull; the reader's page start is a little earlier).
        rec.state.local.page = pos.page;
        rec.state.local.xpath = pos.xpath;
        // A CFI from the server is exact; the page-start one is only filled in when missing.
        if (rec.state.local.cfi.empty()) rec.state.local.cfi = pos.cfi;
    } else {
        rec.state.local = pos;
    }
    ProgressStore::instance().touch();
    saveLastBook();
}

void LibraryManager::applyRecordToLibrary(const BookProgressRecord& rec) {
    touch();
    const ProgressSync::Position& pos = rec.state.local;
    if (lastBook.filePath.equalsIgnoreCase(rec.filePath) || (rec.fileId > 0 && lastBook.fileId == rec.fileId)) {
        lastBook.progress = pos.percent;
        lastBook.page = pos.page > 0 ? pos.page : 1;
        lastBook.xpath = pos.xpath.c_str();
    }
    for (auto& b : masterBooks) {
        if (b.filePath.equalsIgnoreCase(rec.filePath) || (rec.fileId > 0 && b.fileId == rec.fileId)) {
            b.progress = pos.percent;
            b.page = pos.page > 0 ? pos.page : 1;
            b.xpath = pos.xpath.c_str();
        }
    }
}

bool LibraryManager::loadLastBook() {
    return Storage.loadLastBook(lastBook);
}
