#include "sync_service.h"
#include "sync_internal.h"
#include "../platform/hal/mem_stats.h"
#include "../net/bookorbit_client.h"
#include "../core/book_formats.h"
#include "../platform/hal/buttons.h"
#include "../platform/hal/storage.h"
#include "../ui/i18n.h"
#include "../core/ui_port.h"
#include "../reader/reader.h"
#include "../core/progress_sync.h"
#include "../convert/sd_convert.h"
#include "bookmarks.h"
#include "sync_clock.h"
#include "../ui/text_utils.h"
#include "power_service.h"
#include "wifi_session.h"
#include "../storage/catalog_store.h"
#include "../ui/wifi_messages.h"

// Helpers shared by the parts of SyncService (sync_internal.h).
namespace SyncInternal {

// The client logs in again by itself when the token has expired (HTTP 401) and
// updates ctx.config.authToken in memory: this saves it when a sync step ends,
// so the next session does not start with a refused request and a login.
// (A hash, not a copy: a String kept through the Wi-Fi session would sit in the
// middle of the heap the reader needs in one piece afterwards.)
uint32_t tokenHash(const String& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.length(); i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}

// Logs in if there is no token yet; persists a fresh token.
void ensureToken(AppContext& ctx) {
    if (ctx.config.authToken.length() > 0) return;
    if (ctx.config.bookOrbitUser.length() == 0 || ctx.config.bookOrbitPass.length() == 0) return;
    String freshToken;
    if (BookOrbit.login(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass, freshToken)) {
        ctx.config.authToken = freshToken;
        ctx.config.save();
    }
}

// A full sync works through every book: the files are written once at its end
// (not two JSON files per book), and the saved catalog is read once for all.
bool s_batch = false;   // declared in sync_internal.h

SyncBatch* s_batchState = nullptr;

const std::vector<BookMetadata>* savedCatalog(std::vector<BookMetadata>& local) {
    if (s_batchState) {
        if (!s_batchState->loaded) {
            CatalogStore::load(s_batchState->catalog);
            s_batchState->loaded = true;
        }
        return &s_batchState->catalog;
    }
    if (!CatalogStore::load(local)) return nullptr;
    return &local;
}

// Fills in missing server ids of a record from the library list / cached catalog.
// Exact matches only (server id, path, or full title).
void resolveServerIds(BookProgressRecord& rec) {
    if (LibraryManager::isOfflinePath(rec.filePath)) return;
    auto& lib = LibraryManager::getInstance();
    for (const auto& b : lib.masterBooks) {
        if ((rec.fileId > 0 && b.fileId == rec.fileId) || b.filePath.equalsIgnoreCase(rec.filePath)) {
            if (rec.fileId <= 0 && b.fileId > 0) rec.fileId = b.fileId;
            if (rec.bookId <= 0 && b.bookId > 0) rec.bookId = b.bookId;
        }
    }
    if (rec.fileId > 0 && rec.bookId > 0) return;
    std::vector<BookMetadata> local;
    const std::vector<BookMetadata>* catalog = savedCatalog(local);
    if (!catalog) return;
    for (const auto& c : *catalog) {
        // The format of the record: its file id, or the path it is downloaded to.
        const BookFormatEntry* fmt = nullptr;
        for (const auto& f : c.formats) {
            if ((rec.fileId > 0 && f.fileId == rec.fileId) ||
                LibraryManager::catalogFormatPath(c, f).equalsIgnoreCase(rec.filePath)) {
                fmt = &f;
                break;
            }
        }
        if (fmt) {   // never by the title alone: two books of one title are two books
            if (rec.fileId <= 0) rec.fileId = fmt->fileId;
            if (rec.bookId <= 0) rec.bookId = LibraryManager::catalogBookId(c);
            break;
        }
    }
}

// A CFI means the same thing to the web reader for an EPUB file, and for an FB2
// or AZW3 once converted here (its CFI map carries them over; core/cfi_map.h).
// Other formats (old MOBI, TXT) are read through an EPUB made from them whose
// CFIs stay local.
bool cfiApplies(const BookProgressRecord& rec) {
    if (rec.filePath.length() == 0) return true;
    const auto kind = BookFormats::fromPath(rec.filePath.c_str());
    if (kind == BookFormats::Epub) return true;
    const String map = BookReader::cacheDirFor(rec.filePath) + "/" + convert::kCfiMapName;
    return Storage.exists(map.c_str());
}

bool isOpenInReader(const BookProgressRecord& rec) {
    return Reader.isOpened() && Reader.getOriginalFilePath().equalsIgnoreCase(rec.filePath);
}

// Two-way exchange of the book's bookmarks with the web reader's (EPUB only:
// a CFI of the EPUB made here from another format means nothing to the server).
// Deletions made here go first, then the server's list is taken (deleted there,
// new there), then the bookmarks made here go up.
void syncBookmarks(AppContext& ctx, const BookProgressRecord& rec) {
    if (rec.bookId <= 0 || rec.fileId <= 0 || rec.filePath.length() == 0 || !cfiApplies(rec)) return;
    const bool open = Bookmarks.bookPath().length() > 0 && Bookmarks.bookPath().equalsIgnoreCase(rec.filePath);
    BookmarkList other;
    BookmarkList& list = open ? Bookmarks : other;
    if (!open) list.load(rec.filePath);
    auto& cfg = ctx.config;
    bool changed = false;

    const std::vector<int> deleted = list.deletedOnDevice();
    for (int id : deleted) {
        if (!BookOrbit.deleteBookmark(cfg.serverHost, cfg.bookOrbitUser, cfg.bookOrbitPass, cfg.authToken, rec.bookId,
                                      id))
            continue;   // tried again next time
        list.forgetDeleted(id);
        changed = true;
    }

    std::vector<BookOrbitProtocol::Bookmark> server;
    if (!BookOrbit.fetchBookmarks(cfg.serverHost, cfg.bookOrbitUser, cfg.bookOrbitPass, cfg.authToken, rec.bookId,
                                  rec.fileId, server)) {
        if (changed) list.save();
        return;
    }
    std::vector<RemoteBookmark> remote;
    remote.reserve(server.size());
    for (const auto& b : server) {
        RemoteBookmark r;
        r.id = b.id;
        r.cfi = b.cfi.c_str();
        r.title = b.title.c_str();
        remote.push_back(r);
    }
    std::vector<BookOrbitProtocol::Bookmark>().swap(server);
    if (list.mergeRemote(remote)) changed = true;

    int uploaded = 0;
    for (int i = 0; i < list.count(); i++) {
        const Bookmark& b = list.items()[i];
        if (b.serverId > 0 || b.cfi.length() == 0) continue;   // on the server, or no CFI worked out yet
        const String title = b.chapter.length() ? b.chapter : utf8Prefix(b.snippet, 80);
        const int id = BookOrbit.createBookmark(cfg.serverHost, cfg.bookOrbitUser, cfg.bookOrbitPass, cfg.authToken,
                                                rec.bookId, rec.fileId, b.cfi, title);
        if (id <= 0) continue;
        list.setServerId(i, id);
        uploaded++;
        changed = true;
    }
    Serial.printf("[Bookmarks] '%s': %d on the server, %d here, %d uploaded, %d deletes pending\n", rec.title.c_str(),
                  (int)remote.size(), list.count(), uploaded, (int)list.deletedOnDevice().size());
    if (changed) list.save();
}

}  // namespace SyncInternal
