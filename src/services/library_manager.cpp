#include "library_manager.h"
#include "library_internal.h"
#include "../platform/system.h"
#include "../platform/device.h"
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

using namespace LibraryInternal;

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
        for (const auto& e : list)
            if (e.filePath.equalsIgnoreCase(path)) return true;
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
            if ((lastBook.fileId > 0 && b.fileId == lastBook.fileId) ||
                b.filePath.equalsIgnoreCase(lastBook.filePath)) {
                if (b.fileId > 0 && r.fileId <= 0) {
                    r.fileId = b.fileId;
                    lastBook.fileId = b.fileId;
                }
                if (b.docId.length() > 0 && r.docId.length() == 0) {
                    r.docId = b.docId;
                    lastBook.docId = b.docId;
                }
                if (r.author.length() == 0) r.author = b.author;
                break;
            }
        }
        list.push_back(r);
    }

    // 2. Books on the card opened here (they have a position record), newest move first.
    ProgressStore& store = ProgressStore::instance();
    struct OpenedEntry {
        uint32_t changedAt;
        bool isOffline;
        int index;
    };
    std::vector<OpenedEntry> opened;
    auto collectOpened = [&](const std::vector<BookItem>& vec, bool isOffline) {
        for (int i = 0; i < (int)vec.size(); i++) {
            const BookItem& b = vec[i];
            if (!b.isDownloaded) continue;
            if (const BookProgressRecord* rec = store.find(b.fileId, b.filePath)) {
                if (ProgressSync::hasProgress(rec->state.local.percent) || rec->state.local.page > 1) {
                    opened.push_back({rec->state.localChangedAt, isOffline, i});
                }
            }
        }
    };
    collectOpened(masterBooks, false);
    collectOpened(offlineBooks, true);
    std::stable_sort(opened.begin(), opened.end(),
                     [](const OpenedEntry& a, const OpenedEntry& b) { return a.changedAt > b.changedAt; });
    for (const auto& o : opened) {
        if (list.size() >= 4) break;
        const BookItem& b = o.isOffline ? offlineBooks[o.index] : masterBooks[o.index];
        if (!listed(b.filePath)) list.push_back(toInfo(b));
    }

    // 3. Fill up with other books on the card.
    auto fillFrom = [&](const std::vector<BookItem>& vec) {
        for (const auto& b : vec) {
            if (list.size() >= 4) break;
            if (!b.isDownloaded || b.isDirectory || listed(b.filePath)) continue;
            if (!b.formats.empty() && !BookFormats::readable((BookFormats::Kind)b.activeKind()))
                continue;   // not openable yet
            list.push_back(toInfo(b));
        }
    };
    fillFrom(masterBooks);
    fillFrom(offlineBooks);
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
        if (cp < 0x80) {
            out += (char)cp;
            continue;
        }
        int n = 0;
        if (cp < 0x800) {
            buf[n++] = (char)(0xC0 | (cp >> 6));
        } else {
            buf[n++] = (char)(0xE0 | (cp >> 12));
            buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        }
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
    if (c < 0x80) {
        p += 1;
        return c;
    }
    const uint8_t c1 = (uint8_t)p[1];
    if ((c & 0xE0) == 0xC0 && (c1 & 0xC0) == 0x80) {
        p += 2;
        return ((c & 0x1F) << 6) | (c1 & 0x3F);
    }
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

bool LibraryManager::isOfflinePath(const String& path) {
    if (path.length() < 8) return false;
    String lower = path;
    lower.toLowerCase();
    return lower.equals(kOfflineDir) || lower.startsWith("/offline/");
}

BookItem* LibraryManager::formatBookItem() {
    auto& vec = formatsOffline ? offlineBooks : masterBooks;
    return (formatsBook >= 0 && formatsBook < (int)vec.size()) ? &vec[formatsBook] : nullptr;
}

int LibraryManager::rowCount() const { return (int)rows.size(); }

BookItem& LibraryManager::rowAt(int i) {
    const int r = rows[i];
    if (r < 0) return folderRows[-1 - r];
    return (currentFilter == FILTER_OFFLINE) ? offlineBooks[r] : masterBooks[r];
}

int LibraryManager::rowMasterIndex(int i) const {
    return (currentFilter != FILTER_OFFLINE && i >= 0 && i < (int)rows.size() && rows[i] >= 0) ? rows[i] : -1;
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
    for (const auto& f : formats)
        if (f.fileId > 0 || f.url.length() > 0) return true;
    return false;
}

bool BookItem::anyOnCard() const {
    for (const auto& f : formats)
        if (f.onCard) return true;
    return isDownloaded;
}

namespace LibraryInternal {

// "/books/Name.epub" -> "/books/Name" (the part shared by all formats of a book).
String withoutExtension(const String& path) {
    const int slash = path.lastIndexOf('/');
    const int dot = path.lastIndexOf('.');
    return dot > slash + 1 ? path.substring(0, dot) : path;
}

// Folded "<dir>/<stem>": formats of one book share it.
std::string bookKey(const String& path) { return std::string(foldString(withoutExtension(path)).c_str()); }

bool isBookFile(const String& name) { return BookFormats::fromPath(name.c_str()) != BookFormats::Unknown; }

}  // namespace LibraryInternal

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
        if (best < 0 || rec->state.localChangedAt > bestAt) {
            best = i;
            bestAt = rec->state.localChangedAt;
        }
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
                                   BookFormats::rank((BookFormats::Kind)b.formats[idx].kind))
                idx = i;
        }
        return idx;
    };
    auto readable = [](const BookFormatSlot& f) { return BookFormats::readable((BookFormats::Kind)f.kind); };
    if (pick < 0) pick = best([&](const BookFormatSlot& f) { return f.onCard && readable(f); });
    if (pick < 0)
        pick = best([&](const BookFormatSlot& f) { return readable(f) && (f.fileId > 0 || f.url.length() > 0); });
    if (pick < 0) pick = best([](const BookFormatSlot& f) { return f.onCard; });
    if (pick < 0) pick = 0;
    selectFormat(b, pick);
}

void LibraryManager::openFormats(int idx) {
    const bool fromOffline = (currentFilter == FILTER_OFFLINE);
    auto& src = fromOffline ? offlineBooks : masterBooks;
    if (idx < 0 || idx >= (int)src.size()) return;
    if (!currentFolder.equals("@formats")) {
        formatsReturnFolder = currentFolder;
        formatsReturnIndex = selectedBookIndex;
        formatsOffline = fromOffline;
    }
    formatsBook = idx;
    currentFolder = "@formats";
    Serial.printf("[Library] Formats of '%s': %u\n", src[idx].title.c_str(), (unsigned)src[idx].formats.size());
    // Start on the active format (row 0 is "..").
    selectedBookIndex = src[idx].activeFormat >= 0 ? src[idx].activeFormat + 1 : 1;
    rebuildRows();
}

void LibraryManager::closeFormats() {
    if (!currentFolder.equals("@formats")) return;
    currentFolder =
            formatsReturnFolder.length() > 0 ? formatsReturnFolder : String(formatsOffline ? kOfflineDir : "/books");
    selectedBookIndex = formatsReturnIndex;
    formatsBook = -1;
    formatsOffline = false;
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
        if (withoutExtension(last.filePath).equalsIgnoreCase(withoutExtension(entry.fullPath)) &&
            last.author.length() > 0)
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
    CatalogStore::load(catalog);
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
        if (idx < 0) {
            auto it = byKey.find(bookKey(destPath));
            if (it != byKey.end()) idx = it->second;
        }
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
                if ((f.fileId > 0 && sl.fileId == f.fileId) || (sl.fileId <= 0 && sl.kind == f.kind)) {
                    slot = &sl;
                    break;
                }
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
        if (!item.isDownloaded || item.coverPath.length() == 0)
            item.coverPath = "/covers/" + catalogStem(b.title, bookId) + ".jpg";
        item.coverUrl = std::move(b.coverUrl);
        if (bookId > 0) item.bookId = bookId;
        if (item.author.length() == 0) item.author = std::move(b.author);
        if (b.series.length() > 0) {
            item.series = std::move(b.series);
            item.seriesIndex = b.seriesIndex;
        }
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
    scanOfflineBooks();
    Serial.printf("[Library] Master list: %d books (%d in the catalog, %d libraries, %d offline) in %lu ms\n",
                  (int)masterBooks.size(), (int)catalogSize, (int)libraryNames.size(), (int)offlineBooks.size(),
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
    for (auto& item : offlineBooks) {
        if (store.find(item.fileId, item.filePath)) refreshProgressOf(item);
    }
}

void LibraryManager::scanOfflineBooks() {
    touch();
    offlineBooks.clear();
    if (!Storage.isAvailable()) return;
    Storage.ensureDirectories();
    collectAllLocalBooks(kOfflineDir, 0, offlineBooks, nullptr);
    for (auto& item : offlineBooks) chooseFormat(item);
    ProgressStore& store = ProgressStore::instance();
    for (auto& item : offlineBooks) {
        if (store.find(item.fileId, item.filePath)) refreshProgressOf(item);
    }
}

void LibraryManager::refreshLocalFilesIfChanged() {
    if (!_scanned) {
        scanLocalBooks();
        return;
    }
    if (!_cardMayHaveChanged) {
        Serial.println("[Library] Card unchanged since the last walk: list kept (P3)");
        return;
    }
    refreshLocalFiles();
}

void LibraryManager::refreshLocalFiles() {
    _cardMayHaveChanged = false;
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
            if (onCard != f.onCard) {
                f.onCard = onCard;
                changed = true;
            }
        }
        if (b.formats.empty()) {   // not expected for a book, kept as before
            auto it = foundByPath.find(foldString(b.filePath).c_str());
            const bool onCard = it != foundByPath.end();
            if (onCard) matched[it->second] = true;
            if (onCard != b.isDownloaded) {
                b.isDownloaded = onCard;
                changed = true;
            }
        }
        // A new format of a book already in the list ("Name.fb2" next to "Name.epub").
        auto kt = foundByKey.find(bookKey(b.filePath));
        if (kt != foundByKey.end()) {
            for (const auto& nf : found[kt->second].formats) {
                bool known = false;
                for (const auto& f : b.formats) known = known || f.kind == nf.kind;
                if (!known) {
                    b.formats.push_back(nf);
                    changed = true;
                }
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
            if (keep >= 0 && keep < (int)b.formats.size() && b.formats[keep].onCard)
                selectFormat(b, keep);
            else
                chooseFormat(b);
        }
        i++;
    }
    for (size_t i = 0; i < found.size(); i++) {
        if (!matched[i]) {
            masterBooks.push_back(found[i]);
            chooseFormat(masterBooks.back());
            added++;
        }
    }
    if (added) refreshProgress();
    scanOfflineBooks();
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
        for (int i : visible)
            if (!skipEmpty || field(masterBooks[i]).length() > 0) v.push_back(i);
        std::sort(v.begin(), v.end(),
                  [&](int a, int b) { return foldCompare(field(masterBooks[a]), field(masterBooks[b])) < 0; });
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
        if (l >= 0)
            libSeen[l] = true;
        else
            unassigned = true;
    }
    for (bool s : libSeen) c.libraries += s ? 1 : 0;
    if (unassigned && c.libraries > 0) c.libraries++;   // the "[no library]" group
    counts = c;
}

void LibraryManager::rebuildFilteredIndices() {
    rebuildRows();
    _countsDirty = true;
}

bool LibraryManager::releaseListsForReader() {
    if (!Platform::kMemory.wifiTakesReaderMemory) return false;
    if (masterBooks.capacity() == 0 && offlineBooks.capacity() == 0 && libraryNames.capacity() == 0 &&
        rows.capacity() == 0 && folderRows.capacity() == 0)
        return false;
    Serial.printf("[Library] Reader memory: releasing %u catalog books and %u offline books\n",
                  (unsigned)masterBooks.size(), (unsigned)offlineBooks.size());
    if (currentFolder.equals("@formats")) {
        if (const BookItem* owner = formatBookItem()) _releasedFormatStem = withoutExtension(owner->filePath);
    }
    std::vector<int>().swap(rows);
    std::vector<BookItem>().swap(folderRows);
    std::vector<BookItem>().swap(masterBooks);
    std::vector<BookItem>().swap(offlineBooks);
    std::vector<String>().swap(libraryNames);
    _scanned = false;
    touch();
    return true;
}

void LibraryManager::scanLocalBooks(bool forceMasterScan) {
    if (!Storage.isAvailable()) {
        Serial.println("[Library] MicroSD storage not available.");
        rows.clear();
        folderRows.clear();
        return;
    }
    if (currentFolder.length() == 0) currentFolder = (currentFilter == FILTER_OFFLINE) ? kOfflineDir : "/books";
    if (masterBooks.empty() || forceMasterScan || !_scanned)
        buildMaster();
    else {
        refreshProgress();   // positions changed by reading / syncing since the last list
        if (currentFilter == FILTER_OFFLINE) scanOfflineBooks();
    }
    _scanned = true;
    if (_releasedFormatStem.length() > 0) {
        if (currentFolder.equals("@formats")) {
            // A just-downloaded file can change master-list indices. Restore
            // this book by its format-independent path, never by the old index.
            const auto& source = formatsOffline ? offlineBooks : masterBooks;
            formatsBook = -1;
            for (int i = 0; i < (int)source.size(); ++i) {
                if (withoutExtension(source[i].filePath).equalsIgnoreCase(_releasedFormatStem)) {
                    formatsBook = i;
                    break;
                }
            }
            if (formatsBook < 0) {
                currentFolder = formatsReturnFolder.length() > 0 ? formatsReturnFolder
                                                                 : String(formatsOffline ? kOfflineDir : "/books");
                selectedBookIndex = formatsReturnIndex;
            }
        }
        _releasedFormatStem = String();
    }
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
        for (int i = 0; i < (int)masterBooks.size(); i++)
            if (visible(masterBooks[i])) v.push_back(i);
        std::sort(v.begin(), v.end(),
                  [&](int a, int b) { return foldCompare(name(masterBooks[a]), name(masterBooks[b])) < 0; });
        int emptyCount = 0;
        for (size_t k = 0; k < v.size();) {
            const String g = trimmed(name(masterBooks[v[k]]));
            size_t e = k + 1;
            while (e < v.size() && foldCompare(trimmed(name(masterBooks[v[e]])), g) == 0) e++;
            if (g.length() == 0)
                emptyCount += (int)(e - k);
            else
                addFolder(folderItem(g, prefix + g, (int)(e - k)));
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
        const BookItem* parent = formatBookItem();
        if (!parent) {
            currentFolder = formatsReturnFolder.length() > 0 ? formatsReturnFolder
                                                             : String(formatsOffline ? kOfflineDir : "/books");
            formatsBook = -1;
            formatsOffline = false;
            rebuildRows();
            return;
        }
        const BookItem& book = *parent;
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
        for (const auto& b : masterBooks)
            if (b.libraryIdx >= 0 && visible(b)) {
                anyLib = true;
                break;
            }
        groups([this](const BookItem& b) -> const String& { return libraryOf(b); },
               "@library:", anyLib ? tr(MsgId::CATALOG_UNKNOWN_LIBRARY) : nullptr, "__none__");
    } else if (f.startsWith("@library:")) {
        upItem("@libraries", tr(MsgId::CATALOG_BY_LIBRARIES));
        const String target = f.substring(9);
        const bool none = target == "__none__";
        members(
                [&](const BookItem& b) {
                    const String& l = libraryOf(b);
                    return none ? l.length() == 0 : foldCompare(l, target) == 0;
                },
                byTitle);
    } else if (f.equalsIgnoreCase("@authors")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        groups([](const BookItem& b) -> const String& { return b.author; },
               "@author:", tr(MsgId::CATALOG_UNKNOWN_AUTHOR), "");
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
        groups([](const BookItem& b) -> const String& { return b.series; }, "@series:", tr(MsgId::CATALOG_STANDALONE),
               "__none__");
    } else if (f.startsWith("@series:")) {
        upItem("@series", tr(MsgId::CATALOG_BY_SERIES));
        const String target = f.substring(8);
        const bool none = target == "__none__";
        members(
                [&](const BookItem& b) {
                    return none ? trimmed(b.series).length() == 0 : foldCompare(trimmed(b.series), target) == 0;
                },
                none ? std::function<bool(int, int)>(byTitle) : std::function<bool(int, int)>(bySeriesThenTitle));
    } else if (currentFilter == FILTER_SEARCH || f.equalsIgnoreCase("@search")) {
        upItem("@catalog", tr(MsgId::HOME_CAT_CATALOG));
        const String needle = foldString(searchQuery);
        members(
                [&](const BookItem& b) {
                    return needle.length() == 0 || foldString(b.title).indexOf(needle) >= 0 ||
                           foldString(b.author).indexOf(needle) >= 0 || foldString(b.series).indexOf(needle) >= 0;
                },
                byTitle);
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
                   parentIsTop ? String(tr(currentFilter == FILTER_FOLDERS ? MsgId::CATALOG_BY_FOLDERS
                                                                           : MsgId::CATALOG_ALL_BOOKS))
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
                for (const auto& mb : masterBooks)
                    if (mb.isDownloaded && mb.filePath.startsWith(prefix)) cnt++;
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
                if (visible(masterBooks[idx]) && std::find(files.begin(), files.end(), idx) == files.end())
                    files.push_back(idx);
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
        std::sort(dirs.begin(), dirs.end(),
                  [](const BookItem& a, const BookItem& b) { return foldCompare(a.title, b.title) < 0; });
        std::sort(files.begin(), files.end(), byTitle);
        for (const auto& d : dirs) addFolder(d);
        for (int i : files) rows.push_back(i);
        for (const auto& it : looseFiles) addFolder(it);
    } else if (currentFilter == FILTER_OFFLINE) {
        for (int i = 0; i < (int)offlineBooks.size(); i++) {
            if (!visible(offlineBooks[i])) continue;
            rows.push_back(i);
        }
        std::stable_sort(rows.begin(), rows.end(),
                         [&](int a, int b) { return foldCompare(offlineBooks[a].title, offlineBooks[b].title) < 0; });
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
