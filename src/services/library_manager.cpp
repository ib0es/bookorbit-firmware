#include "library_manager.h"
#include "progress_sync.h"
#include "sync_clock.h"
#include "../reader/simple_reader.h"
#include "../reader/epub_extractor.h"
#include "../ui/jpeg_viewer.h"
#include "../net/bookorbit_client.h"
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

    // 2. Check Book Orbit / downloaded cover in /covers/
    if (title.length() > 0) {
        String safeTitle = title;
        safeTitle.replace("/", "_");
        safeTitle.replace("\\", "_");
        safeTitle.replace(":", "_");
        safeTitle.replace("*", "_");
        safeTitle.replace("?", "_");
        safeTitle.replace("\"", "_");
        safeTitle.trim();

        String cov1 = "/covers/" + safeTitle + ".jpg";
        if (isValidCoverFile(cov1)) return cov1;
        String cov2 = "/covers/" + safeTitle + ".jpeg";
        if (isValidCoverFile(cov2)) return cov2;
    }

    // 3. Check EPUB extracted cache cover in /covers/<basename>.jpg
    if (filePath.length() > 0) {
        String cPath = EpubExtractor::getCoverPath(filePath);
        if (isValidCoverFile(cPath)) return cPath;

        String cPathJpeg = cPath;
        if (cPathJpeg.endsWith(".jpg")) {
            cPathJpeg = cPathJpeg.substring(0, cPathJpeg.length() - 4) + ".jpeg";
            if (isValidCoverFile(cPathJpeg)) return cPathJpeg;
        }

        // 4. Extract embedded cover from EPUB if allowed. A cover that cannot be
        //    drawn (progressive JPEG, PNG) is not extracted again this session:
        //    the home screen asks for it on every redraw.
        static std::vector<String> s_unusableEpubCovers;
        bool knownBad = false;
        for (const auto& p : s_unusableEpubCovers) if (p == cPath) { knownBad = true; break; }
        if (allowExtract && !knownBad && EpubExtractor::isEpub(filePath) && Storage.exists(filePath.c_str())) {
            if (EpubExtractor::extractCoverOnly(filePath, cPath) && isValidCoverFile(cPath)) return cPath;
            s_unusableEpubCovers.push_back(cPath);
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

    // /cache/<stem>.*: text, TOC, spine, CFI map, temp files and every page
    // index (all font sizes and screen geometries, so one SD card shared by
    // several devices is cleaned too). Matched by exact suffix so that a book
    // whose name starts the same ("Name" vs "Name (2020)") is left alone.
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
        removeCoverStem(stem);                                           // extracted from the EPUB
        removeCoverStem(safeFileName(book.title, String(book.fileId)));  // downloaded from the server
        // An explicit cover in /covers/ (never a cover.jpg in the book's folder).
        if (book.coverPath.startsWith("/covers/")) removeCoverStem(stemOf(book.coverPath));
    }
    JpegViewer::clearCache();
}

namespace {

// Stems (file name without extension) of every book file under `dir`.
void collectBookStems(const String& dir, int depth, std::vector<String>& out, int& files) {
    if (depth > 4) return;
    for (const DirEntry& e : Storage.listDirectory(dir)) {  // books and folders only
        if (e.isDirectory) {
            collectBookStems(e.fullPath, depth + 1, out, files);
        } else {
            out.push_back(stemOf(e.name));
            files++;
        }
    }
}

bool contains(const std::vector<String>& v, const String& s) {
    for (const String& x : v) if (x == s) return true;
    return false;
}

// The book stem a /cache file belongs to, "" if the name is not one we write.
String cacheOwner(const String& n) {
    const char* exact[] = { ".toc.tmp", ".toc", ".spine", ".cfimap" };
    for (const char* sfx : exact) {
        if (n.endsWith(sfx)) return n.substring(0, n.length() - strlen(sfx));
    }
    const int t = n.lastIndexOf(".txt");  // .txt, .txt.idx, .txt_f1[_WxH].idx, .txt.tmp
    if (t > 0) return n.substring(0, t);
    return "";
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

    std::vector<String> books;
    int bookFiles = 0;
    collectBookStems("/", 0, books, bookFiles);
    if (bookFiles == 0) {
        // An empty or unreadable card would make every cache file look orphaned.
        Serial.println("[Library] Cleanup skipped: no book files found.");
        return;
    }

    std::vector<String> covers = books;
    std::vector<BookMetadata> catalog;
    Storage.loadCatalog(catalog);
    for (const auto& b : catalog) covers.push_back(safeFileName(b.title, b.id));
    if (lastBook.coverPath.startsWith("/covers/")) covers.push_back(stemOf(lastBook.coverPath));
    for (const auto& b : masterBooks) {
        if (b.coverPath.startsWith("/covers/")) covers.push_back(stemOf(b.coverPath));
    }

    int removed = 0;
    for (const String& n : Storage.listFileNames("/cache")) {
        // Leftover scratch files of an interrupted EPUB conversion.
        const bool scratch = (n.startsWith(".") && n.endsWith(".tmp")) ||
                             // page indices of the old U8g2 fonts (<stem>.txt_f<0..3>[_WxH].idx)
                             (n.indexOf(".txt_f") > 0 && n.endsWith(".idx"));
        const String owner = scratch ? String() : cacheOwner(n);
        if (scratch || (owner.length() > 0 && !contains(books, owner))) {
            removeIfExists("/cache/" + n);
            removed++;
        }
    }
    for (const String& n : Storage.listFileNames("/covers")) {
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

std::vector<RecentBookInfo> LibraryManager::getRecentBooks() {
    std::vector<RecentBookInfo> list;
    auto toInfo = [&](const BookItem& b) {
        RecentBookInfo r;
        r.title = b.title;
        r.author = b.author;
        r.filePath = b.filePath;
        r.coverPath = resolveCoverPath(b.filePath, b.title, b.coverPath, true);
        r.downloadUrl = b.downloadUrl;
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
        r.coverPath = resolveCoverPath(r.filePath, r.title, lastBook.coverPath, true);
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

bool isCompleted(const BookItem& b) { return b.progress >= 0.90f; }

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
    refreshCounts();
}

static void collectAllLocalBooks(const String& dir, int depth, std::vector<BookItem>& out, int* topFolders) {
    if (depth > 3) return;
    for (const DirEntry& entry : Storage.listDirectory(dir)) {   // folders and book files only
        if (entry.name.startsWith(".")) continue;
        if (entry.isDirectory) {
            if (depth == 0 && topFolders) (*topFolders)++;
            collectAllLocalBooks(entry.fullPath, depth + 1, out, nullptr);
            continue;
        }
        String lower = entry.name;
        lower.toLowerCase();
        if (!lower.endsWith(".epub") && !lower.endsWith(".txt")) continue;   // what the reader opens

        BookItem item;
        const int dot = entry.name.lastIndexOf('.');
        item.title = (dot > 0) ? entry.name.substring(0, dot) : entry.name;
        item.filePath = entry.fullPath;
        item.coverPath = EpubExtractor::getCoverPath(entry.fullPath);
        item.isDownloaded = true;
        const LastBookInfo& last = LibraryManager::getInstance().lastBook;
        if (last.filePath.equalsIgnoreCase(entry.fullPath) && last.author.length() > 0) item.author = last.author;
        out.push_back(item);
    }
}

void LibraryManager::buildMaster() {
    const uint32_t t0 = millis();
    masterBooks.clear();
    libraryNames.clear();
    int folders = 0;
    collectAllLocalBooks("/books", 0, masterBooks, &folders);
    counts.folders = folders;

    // Lookup tables for merging the catalog (was a pairwise scan of both lists).
    std::unordered_map<std::string, int> byPath, byTitle, byFileId;
    auto key = [](const String& s) { return std::string(foldString(s).c_str()); };
    for (int i = 0; i < (int)masterBooks.size(); i++) {
        byPath.emplace(key(masterBooks[i].filePath), i);
        byTitle.emplace(key(masterBooks[i].title), i);
    }

    std::vector<BookMetadata> catalog;
    Storage.loadCatalog(catalog);
    for (const auto& b : catalog) {
        const String destPath = catalogBookPath(b);
        const int bookId = catalogBookId(b);
        int idx = -1;
        if (b.fileId > 0) {
            auto it = byFileId.find(std::to_string(b.fileId));
            if (it != byFileId.end()) idx = it->second;
        }
        if (idx < 0) { auto it = byPath.find(key(destPath)); if (it != byPath.end()) idx = it->second; }
        if (idx < 0) { auto it = byTitle.find(key(b.title)); if (it != byTitle.end()) idx = it->second; }
        if (idx < 0) {
            masterBooks.push_back(BookItem());
            idx = (int)masterBooks.size() - 1;
            BookItem& n = masterBooks.back();
            n.title = b.title;
            n.filePath = destPath;
            n.isDownloaded = false;   // a downloaded copy would have matched a file of the scan
            byPath.emplace(key(destPath), idx);
            byTitle.emplace(key(b.title), idx);
        }
        BookItem& item = masterBooks[idx];
        // The cover is resolved when it is drawn (home card); the server one lands here.
        if (!item.isDownloaded || item.coverPath.length() == 0) item.coverPath = "/covers/" + safeFileName(b.title, b.id) + ".jpg";
        item.downloadUrl = b.downloadUrl;
        item.coverUrl = b.coverUrl;
        if (b.fileId > 0) { item.fileId = b.fileId; byFileId.emplace(std::to_string(b.fileId), idx); }
        if (bookId > 0) item.bookId = bookId;
        if (item.author.length() == 0) item.author = b.author;
        if (b.series.length() > 0) { item.series = b.series; item.seriesIndex = b.seriesIndex; }
        if (b.library.length() > 0) item.libraryIdx = (int16_t)internLibrary(b.library);
        // Server snapshot; this device's own position (if any) wins in refreshProgress().
        item.progress = b.lastProgress;
        item.page = b.lastPage > 0 ? b.lastPage : 1;
        item.xpath = b.xpath;
    }
    refreshProgress();
    for (const auto& item : masterBooks) {
        if (lastBook.fileId <= 0 && item.fileId > 0 && lastBook.filePath.equalsIgnoreCase(item.filePath)) {
            lastBook.fileId = item.fileId;
        }
    }
    Serial.printf("[Library] Master list: %d books (%d in the catalog, %d libraries) in %lu ms\n",
                  (int)masterBooks.size(), (int)catalog.size(), (int)libraryNames.size(),
                  (unsigned long)(millis() - t0));
}

void LibraryManager::refreshProgress() {
    ProgressStore& store = ProgressStore::instance();
    for (auto& item : masterBooks) {
        if (const BookProgressRecord* rec = store.find(item.fileId, item.filePath)) {
            item.progress = rec->state.local.percent;
            item.page = rec->state.local.page > 0 ? rec->state.local.page : 1;
            item.xpath = rec->state.local.xpath.c_str();
            if (item.fileId <= 0 && rec->fileId > 0) item.fileId = rec->fileId;
            if (item.bookId <= 0 && rec->bookId > 0) item.bookId = rec->bookId;
        }
    }
}

void LibraryManager::refreshLocalFiles() {
    if (!Storage.isAvailable()) return;
    std::vector<BookItem> found;
    int folders = 0;
    collectAllLocalBooks("/books", 0, found, &folders);
    counts.folders = folders;

    std::unordered_map<std::string, int> foundByPath;
    for (int i = 0; i < (int)found.size(); i++) foundByPath.emplace(foldString(found[i].filePath).c_str(), i);
    std::vector<bool> matched(found.size(), false);

    int added = 0, gone = 0;
    for (size_t i = 0; i < masterBooks.size();) {
        BookItem& b = masterBooks[i];
        auto it = foundByPath.find(foldString(b.filePath).c_str());
        const bool onCard = it != foundByPath.end();
        if (onCard) matched[it->second] = true;
        if (onCard != b.isDownloaded) {
            if (!onCard && b.downloadUrl.length() == 0) {   // a local-only book removed from the card
                masterBooks.erase(masterBooks.begin() + i);
                gone++;
                continue;
            }
            b.isDownloaded = onCard;
        }
        i++;
    }
    for (size_t i = 0; i < found.size(); i++) {
        if (!matched[i]) { masterBooks.push_back(found[i]); added++; }
    }
    if (added) refreshProgress();
    refreshCounts();
    if (added || gone) Serial.printf("[Library] Card changed: %d new books, %d removed\n", added, gone);
}

void LibraryManager::refreshCounts() {
    Counts c;
    c.folders = counts.folders;
    std::vector<int> visible;
    visible.reserve(masterBooks.size());
    for (int i = 0; i < (int)masterBooks.size(); i++) {
        const BookItem& b = masterBooks[i];
        if (isCompleted(b)) c.completed++;
        if (hideCompleted && isCompleted(b)) continue;
        visible.push_back(i);
        c.total++;
        if (b.isDownloaded) c.downloaded++;
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
    refreshCounts();
}

void LibraryManager::scanLocalBooks(bool forceMasterScan) {
    if (!Storage.isAvailable()) {
        Serial.println("[Library] MicroSD storage not available.");
        rows.clear();
        folderRows.clear();
        return;
    }
    if (currentFolder.length() == 0) currentFolder = "/books";
    if (masterBooks.empty() || forceMasterScan) buildMaster();
    else refreshProgress();   // positions changed by reading / syncing since the last list
    rebuildRows();
    refreshCounts();
}

void LibraryManager::rebuildRows() {
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
    auto visible = [&](const BookItem& b) { return !(hideCompleted && isCompleted(b)); };
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
    if (f.equalsIgnoreCase("@libraries")) {
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
            String lower = e.name;
            lower.toLowerCase();
            if (!lower.endsWith(".epub") && !lower.endsWith(".txt")) continue;
            int idx = -1;
            for (int i = 0; i < (int)masterBooks.size(); i++) {
                if (masterBooks[i].filePath.equalsIgnoreCase(e.fullPath)) { idx = i; break; }
            }
            if (idx >= 0) {
                if (visible(masterBooks[idx])) files.push_back(idx);
            } else {
                BookItem it;
                const int dot = e.name.lastIndexOf('.');
                it.title = dot > 0 ? e.name.substring(0, dot) : e.name;
                it.filePath = e.fullPath;
                it.isDownloaded = true;
                looseFiles.push_back(it);
            }
        }
        std::sort(dirs.begin(), dirs.end(), [](const BookItem& a, const BookItem& b) { return foldCompare(a.title, b.title) < 0; });
        std::sort(files.begin(), files.end(), byTitle);
        for (const auto& d : dirs) addFolder(d);
        for (int i : files) rows.push_back(i);
        for (const auto& it : looseFiles) addFolder(it);
    } else {
        // "/books": all books, downloaded or completed (master order: SD first, then the catalog).
        for (int i = 0; i < (int)masterBooks.size(); i++) {
            const BookItem& b = masterBooks[i];
            if (currentFilter == FILTER_DOWNLOADED && !b.isDownloaded) continue;
            if (currentFilter == FILTER_COMPLETED ? !isCompleted(b) : !visible(b)) continue;
            rows.push_back(i);
        }
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
                if (item.filePath.equalsIgnoreCase(lastBook.filePath) || item.title.equalsIgnoreCase(lastBook.title)) {
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
    bool ok = Storage.saveLastBook(lastBook);
    ProgressStore::instance().save();
    return ok;
}

// ---------------------------------------------------------------- progress

String LibraryManager::safeFileName(const String& title, const String& fallbackId) {
    String safe = title;
    const char* bad = "/\\:*?\"";   // same set as before, so existing files keep their names
    for (const char* c = bad; *c; c++) safe.replace(String(*c), "_");
    safe.trim();
    if (safe.length() == 0) safe = "book_" + fallbackId;
    return safe;
}

String LibraryManager::catalogBookPath(const BookMetadata& b) {
    String ext = (b.downloadUrl.indexOf(".txt") > 0) ? ".txt" : ".epub";
    return "/books/" + safeFileName(b.title, b.id) + ext;
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
    if (!Reader.isOpened()) return;
    ProgressSync::Position pos = readerPosition(withXPath);
    lastBook.progress = pos.percent;
    lastBook.page = pos.page;
    lastBook.totalPages = Reader.getTotalPages();
    lastBook.xpath = pos.xpath.c_str();

    BookProgressRecord& rec = currentRecord();
    ProgressSync::recordLocalMove(rec.state, pos, SyncClock::nowUtc());
    ProgressStore::instance().touch();
    if (persist) saveLastBook();
}

void LibraryManager::flushReaderPosition() {
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
