#include "library_manager.h"
#include "library_internal.h"
#include "../core/progress_sync.h"
#include "sync_clock.h"
#include "../reader/reader.h"
#include "../convert/sd_convert.h"
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
#include "../storage/progress_store.h"

using namespace LibraryInternal;

namespace LibraryInternal {

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
    // ".jpg.nocover": the marker of a book found to have no cover; ".jpg.tmp" /
    // ".jpg.bmp.tmp": an extraction cut short.
    const char* suffixes[] = {".jpg",      ".jpeg",    ".bmp",         ".jpg.bad",        ".jpg.nocover",
                              ".nocover",  ".jpg.tmp", ".jpg.bmp.tmp", "_thumb_crop.bmp", "_thumb_fit.bmp",
                              "_crop.bmp", "_fit.bmp"};
    for (const char* sfx : suffixes) removeIfExists("/covers/" + stem + sfx);
}

}  // namespace LibraryInternal

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
                              n == stem + ".toc" || n == stem + ".toc.tmp" || n == stem + ".spine" ||
                              n == stem + ".cfimap";
            if (mine) removeIfExists("/cache/" + n);
        }
    }

    // U7: the book's bookmarks go with it (a download again brings back the
    // server's copies).
    removeIfExists("/bookmarks/" + BookReader::cacheKeyFor(book.filePath) + ".json");

    if (removeCover) {
        removeCoverStem(stemOf(BookReader::cacheKeyFor(book.filePath)));   // extracted from the EPUB
        if (book.bookId > 0) removeCoverStem(catalogStem(book.title, book.bookId));  // downloaded from the server
        // An explicit cover in /covers/ (never a cover.jpg in the book's folder).
        if (book.coverPath.startsWith("/covers/")) removeCoverStem(stemOf(book.coverPath));
    }
    // An offline book is known to this device only: its position goes with it
    // (a catalog book keeps its record: the server has it, a download brings it back).
    if (isOfflinePath(book.filePath)) {
        ProgressStore& store = ProgressStore::instance();
        if (store.remove(book.filePath)) {
            store.save();
            Serial.printf("[Library]   position of %s forgotten\n", book.filePath.c_str());
        }
    }
    Ui::coverCacheChanged();
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
    for (const String& x : v)
        if (x == s) return true;
    return false;
}

// The cover stem a /covers file belongs to, "" if the name is not one we write.
String coverOwner(const String& n) {
    const char* sfx[] = {"_thumb_crop.bmp", "_thumb_fit.bmp", "_crop.bmp", "_fit.bmp", ".jpg.bad",
                         ".jpg.nocover",    ".jpg.bmp.tmp",   ".jpg.tmp",  ".jpeg",    ".jpg"};
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
    const bool catalogKnown = CatalogStore::load(catalog) || !Storage.exists("/books/catalog.json");
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
    if (removed > 0) Ui::coverCacheChanged();
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
    LastBookStore::save(lastBook);
}

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
    const char* suffixes[] = {".jpg",           ".jpeg",     ".jpg.bad", "_thumb_crop.bmp",
                              "_thumb_fit.bmp", "_crop.bmp", "_fit.bmp"};
    for (const char* sfx : suffixes) renameIfThere("/covers/" + from + sfx, "/covers/" + to + sfx);
}

}  // namespace

void LibraryManager::migrateLegacyNames(const std::vector<BookMetadata>& catalog) {
    if (catalog.empty() || masterBooks.empty()) return;
    std::unordered_map<std::string, int> local;   // books on the card by folded "<dir>/<stem>"
    for (int i = 0; i < (int)masterBooks.size(); i++) local.emplace(bookKey(masterBooks[i].filePath), i);

    ProgressStore& store = ProgressStore::instance();
    auto hasRecord = [&](const BookMetadata& b) {
        for (const auto& f : b.formats)
            if (f.fileId > 0 && store.find(f.fileId, "")) return true;
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
        if (it == owner.end())
            owner.emplace(oldKey, c);
        else if (!hasRecord(catalog[it->second]) && hasRecord(b))
            it->second = c;
    }
    if (owner.empty()) return;

    const uint32_t t0 = millis();
    int moved = 0;
    // The last book as on the card: at boot the list is built before it is loaded.
    LastBookInfo saved;
    const bool haveSaved = LastBookStore::load(saved);
    bool savedChanged = false;
    for (const auto& o : owner) {
        const BookMetadata& b = catalog[o.second];
        BookItem& item = masterBooks[local[o.first]];
        // The book open in the reader keeps its name (its cache directory is in
        // use): it is renamed when the list is built again, at the next start or sync.
        if (Reader.isOpened() &&
            withoutExtension(Reader.getOriginalFilePath()).equalsIgnoreCase(withoutExtension(item.filePath)))
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
            if (haveSaved && saved.filePath.equalsIgnoreCase(from)) {
                saved.filePath = to;
                savedChanged = true;
            }
            moved++;
        }
        renameCoverStem(oldStem, newStem);
        const String oldCover = "/covers/" + oldStem + ".", newCover = "/covers/" + newStem + ".jpg";
        if (lastBook.coverPath.startsWith(oldCover)) lastBook.coverPath = newCover;
        if (haveSaved && saved.coverPath.startsWith(oldCover)) {
            saved.coverPath = newCover;
            savedChanged = true;
        }
        // The list entry follows the files.
        const int8_t active = item.activeFormat;
        item.filePath = "/books/" + newStem + BookFormats::extension((BookFormats::Kind)item.activeKind());
        item.coverPath = epubCoverPath(item.filePath);
        if (active >= 0) selectFormat(item, active);
    }
    if (moved > 0) {
        if (savedChanged) LastBookStore::save(saved);
        ProgressStore::instance().save();
        Ui::coverCacheChanged();
        touch();
    }
    Serial.printf("[Library] %d files renamed to server ids in %lu ms\n", moved, (unsigned long)(millis() - t0));
}

int LibraryManager::catalogBookId(const BookMetadata& b) {
    int id = b.id.toInt();
    return id > 0 ? id : b.fileId;
}
