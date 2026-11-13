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

using namespace SyncInternal;

// ============================================================================
// Full sync
// ============================================================================
namespace {

void returnFromSync(AppContext& ctx, AppState prevState) { SyncService::showState(ctx, prevState); }

void showConfigError(AppContext& ctx, AppState prevState, const char* what, const char* line1, const char* line2) {
    Serial.printf("[Sync] Not configured: %s\n", what);
    Ui::status(tr(MsgId::SYNC_CONFIG_ERR_TITLE), line1, line2);
    PowerService::waitOrKey(2000);
    SyncService::showState(ctx, prevState);
}

// Record for a format of a catalog entry: by file id or the path it is downloaded to.
BookProgressRecord* recordForCatalogFormat(const BookMetadata& b, const BookFormatEntry& f) {
    ProgressStore& store = ProgressStore::instance();
    const String path = LibraryManager::catalogFormatPath(b, f);
    BookProgressRecord* rec = store.find(f.fileId, path);
    if (rec) {
        if (f.fileId > 0) rec->fileId = f.fileId;
        const int bookId = LibraryManager::catalogBookId(b);
        if (bookId > 0) rec->bookId = bookId;
    }
    return rec;
}

// File ids synced already in this sync (each format of a book is synced on its own).
bool alreadySynced(const std::vector<int>& synced, int fileId) {
    for (int id : synced)
        if (fileId > 0 && id == fileId) return true;
    return false;
}

// Three-way sync of every book that has a progress record on this device.
void syncOpenedBooks(AppContext& ctx, std::vector<int>& synced) {
    auto& records = ProgressStore::instance().records();
    for (size_t i = 0; i < records.size(); i++) {
        if (SyncService::isCancelled() || SyncService::checkSyncCancel()) break;
        BookProgressRecord& rec = records[i];
        if (LibraryManager::isOfflinePath(rec.filePath)) continue;
        if (rec.filePath.length() > 0 && !Storage.exists(rec.filePath.c_str())) continue;   // deleted book
        resolveServerIds(rec);
        if (rec.bookId <= 0) continue;   // matched against the catalog below
        SyncService::syncBookProgress(ctx, rec);   // a failure leaves it queued (dirty)
        if (rec.fileId > 0) synced.push_back(rec.fileId);   // not asked again for its catalog entry
    }
    ProgressStore::instance().save();
}

// Adds saved catalog entries missing from a truncated fresh catalog.
void mergeSavedCatalog(std::vector<BookMetadata>& catalog) {
    std::vector<BookMetadata> saved;
    if (!CatalogStore::load(saved)) return;
    size_t added = 0;
    for (const auto& old : saved) {
        bool present = false;
        for (const auto& b : catalog) {
            if ((old.fileId > 0 && b.fileId == old.fileId) || (old.id.length() > 0 && b.id == old.id)) {
                present = true;
                break;
            }
        }
        if (!present) {
            catalog.push_back(old);
            added++;
        }
    }
    Serial.printf("[Sync] Catalog was truncated: kept %u books from the saved catalog.\n", (unsigned)added);
}

void downloadCovers(AppContext& ctx, const std::vector<BookMetadata>& catalog) {
    auto& lib = LibraryManager::getInstance();
    int downloaded = 0;
    for (const auto& b : catalog) {
        if (SyncService::isCancelled() || SyncService::checkSyncCancel()) break;
        if (b.coverUrl.length() == 0) continue;

        const String coverPath =
                "/covers/" + LibraryManager::catalogStem(b.title, LibraryManager::catalogBookId(b)) + ".jpg";
        const String destPath = LibraryManager::catalogBookPath(b);
        const bool wanted =
                lib.lastBook.filePath.equalsIgnoreCase(destPath) || Storage.exists(destPath.c_str()) || downloaded < 4;
        if (wanted && !LibraryManager::isValidCoverFile(coverPath) &&
            !LibraryManager::coverKnownUnusable(coverPath, b.coverUrl)) {
            Serial.printf("[Sync] Downloading cover thumbnail: %s -> %s\n", b.title.c_str(), coverPath.c_str());
            if (BookOrbit.downloadCover(ctx.config.serverHost, b.coverUrl, coverPath, ctx.config.bookOrbitUser,
                                        ctx.config.bookOrbitPass, ctx.config.authToken, SyncService::checkSyncCancel)) {
                downloaded++;
                if (!LibraryManager::isValidCoverFile(coverPath))
                    LibraryManager::markCoverUnusable(coverPath, b.coverUrl);
            }
        }
    }
}

}  // namespace

void SyncService::syncWithBookOrbit(AppContext& ctx) {
    KeepToken keepToken(ctx);
    SyncBatch batch;
    s_batchState = &batch;
    struct ClearBatch {
        ~ClearBatch() { s_batchState = nullptr; }
    } clearBatch;
    WifiSession::prepare();
    AppState prevState = ctx.currentState;
    s_syncCancelled = false;
    ctx.currentState = STATE_SYNCING;

    // Debounce / wait for release of any buttons pressed when entering sync
    uint32_t waitStart = millis();
    while ((Buttons.isPressed(BTN_CONFIRM) || Buttons.isPressed(BTN_RIGHT) || Buttons.isPressed(BTN_BACK)) &&
           (millis() - waitStart < 250)) {
        Buttons.update();
        delay(10);
    }
    Buttons.clearEvents();

    auto& lib = LibraryManager::getInstance();
    if (Reader.isOpened()) lib.flushReaderPosition();

    if (ctx.config.wifiSsid.length() == 0) {
        showConfigError(ctx, prevState, "Wi-Fi", tr(MsgId::SYNC_ERR_NO_WIFI_L1), tr(MsgId::SYNC_ERR_NO_WIFI_L2));
        return;
    }
    if (ctx.config.serverHost.length() == 0) {
        showConfigError(ctx, prevState, "server", tr(MsgId::SYNC_ERR_NO_SERVER_L1), tr(MsgId::SYNC_ERR_NO_SERVER_L2));
        return;
    }

    Ui::status(tr(MsgId::SYNC_MODAL_TITLE), tr(MsgId::SYNC_CONNECTING_WIFI_PREFIX), ctx.config.wifiSsid.c_str(),
               nullptr, nullptr, tr(MsgId::COMMON_CANCEL));
    if (!BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 10000, checkSyncCancel)) {
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }
        Serial.println("[Sync] Wi-Fi connection failed.");
        Ui::status(tr(MsgId::WIFI_ERR_TITLE), tr(MsgId::SYNC_ERR_WIFI_CONNECT_L1),
                   wifiFailureLine(BookOrbit.lastWifiFailure(), tr(MsgId::SYNC_ERR_WIFI_CONNECT_L2)));
        PowerService::waitOrKey(1800);
        lib.scanLocalBooks();
        returnFromSync(ctx, prevState);
        return;
    }
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    MemStats::log("sync: Wi-Fi up");
    Ui::status(tr(MsgId::SYNC_MODAL_TITLE), tr(MsgId::SYNC_WIFI_CONNECTED), tr(MsgId::SYNC_FETCHING_CATALOG_PROGRESS),
               ctx.config.serverHost.c_str(), nullptr, tr(MsgId::COMMON_CANCEL));
    ensureToken(ctx);
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    // 1. Reading progress of every book opened on this device, before the catalog:
    //    it must not depend on the catalog fitting into memory, and the heap is
    //    at its largest now.
    std::vector<int> synced;
    syncOpenedBooks(ctx, synced);
    // The saved catalog read for the ids goes before the fresh one is fetched (the
    // heap is at its tightest then); the books of step 3 get their ids from it.
    std::vector<BookMetadata>().swap(batch.catalog);
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    // 2. OPDS catalog (re-login once if the token went stale).
    std::vector<BookMetadata> catalog;
    bool catOk = BookOrbit.fetchCatalog(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass,
                                        ctx.config.authToken, catalog);
    if (!catOk && ctx.config.bookOrbitUser.length() > 0 && ctx.config.bookOrbitPass.length() > 0) {
        ctx.config.authToken = "";
        ensureToken(ctx);
        catOk = BookOrbit.fetchCatalog(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass,
                                       ctx.config.authToken, catalog);
    }

    if (catOk) {
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }
        Serial.printf("[Sync] Catalog fetched: %d books available.\n", (int)catalog.size());
        MemStats::log("sync: catalog parsed");
        const CatalogFetchInfo& ci = BookOrbit.lastCatalog;
        const int fetched = (int)catalog.size();   // before the saved books are merged back in

        if (catalog.empty() && !ci.cleanEmpty()) {
            // Something went wrong on the device side (stream error, unrecognised
            // entries): keep the saved catalog and show why.
            Serial.printf(
                    "[Sync] Empty catalog looks wrong: http=%d err=%d bytes=%u skipped=%u heap=%u/%u truncated=%d\n",
                    ci.httpCode, ci.streamError, (unsigned)ci.bytes, (unsigned)ci.skipped, (unsigned)ci.heapStart,
                    (unsigned)ci.heapMin, (int)ci.truncated);
            char l1[64], l2[64];
            snprintf(l1, sizeof(l1), tr(MsgId::SYNC_CATALOG_PARSE_FMT1), ci.httpCode, (unsigned)ci.bytes,
                     (unsigned)ci.skipped);
            snprintf(l2, sizeof(l2), tr(MsgId::SYNC_CATALOG_PARSE_FMT2), (unsigned)(ci.heapStart / 1024),
                     (unsigned)(ci.heapMin / 1024), ci.streamError);
            Ui::status(tr(MsgId::SYNC_CATALOG_PARSE_ERR_TITLE), l1, l2, tr(MsgId::SYNC_CATALOG_KEPT));
            PowerService::waitOrKey(4000);
            catalog.clear();
            CatalogStore::load(catalog);
        } else if (ci.truncated) {
            // Out of memory part-way: keep the books we knew about but did not get now.
            mergeSavedCatalog(catalog);
        }
        // Files downloaded by older firmware (named by title) take their id names
        // before their positions are looked up by those names.
        lib.migrateLegacyNames(catalog);

        // 3. Books on the card never opened here (downloaded by older firmware, or
        //    copied over) get a record and the server's position: their first sync
        //    is a pull. Each format separately: a book read here as FB2 and on the
        //    web as EPUB has two positions. Books only on the server are not asked
        //    about: their position comes when they are downloaded.
        for (const auto& b : catalog) {
            if (s_syncCancelled || checkSyncCancel()) break;
            const int bookId = LibraryManager::catalogBookId(b);
            for (const auto& f : b.formats) {
                if (s_syncCancelled || checkSyncCancel()) break;
                if (alreadySynced(synced, f.fileId)) continue;
                const String path = LibraryManager::catalogFormatPath(b, f);
                if (!Storage.exists(path.c_str())) continue;   // not on the card
                BookProgressRecord* rec = recordForCatalogFormat(b, f);
                if (!rec) rec = &ProgressStore::instance().getOrCreate(f.fileId, bookId, path, b.title);
                syncBookProgress(ctx, *rec);
                if (f.fileId > 0) synced.push_back(f.fileId);
            }
        }
        CatalogStore::save(catalog);
        ProgressStore::instance().save();

        downloadCovers(ctx, catalog);
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }

        char catInfo[64];
        snprintf(catInfo, sizeof(catInfo), tr(MsgId::SYNC_FOUND_BOOKS_FMT), (int)catalog.size());
        if (ci.truncated) {
            // U6: the server has more than the device took in (its limit, or memory):
            // said, not only logged.
            char cut[96];
            snprintf(cut, sizeof(cut), tr(MsgId::SYNC_CATALOG_TRUNCATED_FMT), fetched);
            Serial.printf("[Sync] Catalog incomplete: %d books taken from the server\n", fetched);
            Ui::status(tr(MsgId::SYNC_SUCCESS_TITLE), catInfo, cut, tr(MsgId::SYNC_CATALOG_UPDATED_L2));
            PowerService::waitOrKey(3000);
        } else {
            Ui::status(tr(MsgId::SYNC_SUCCESS_TITLE), catInfo, tr(MsgId::SYNC_CATALOG_UPDATED_L2));
            PowerService::waitOrKey(800);
        }
    } else {
        Serial.println("[Sync] Book Orbit server not reachable or catalog fetch failed!");
        Ui::status(tr(MsgId::SYNC_SERVER_UNREACHABLE_TITLE), tr(MsgId::SYNC_SERVER_UNREACHABLE_L1),
                   tr(MsgId::SYNC_SERVER_UNREACHABLE_L2));
        PowerService::waitOrKey(1800);
    }

    // The fetched catalog is on the card now: free it (and the per-book results)
    // before the book list is rebuilt, so the list's long-lived strings are not
    // allocated between short-lived ones (a fragmented heap after a sync had
    // only ~50 KB in one piece of ~110 KB free).
    std::vector<BookMetadata>().swap(catalog);
    std::vector<int>().swap(synced);
    BookOrbit.disconnectWiFi();
    MemStats::log("sync: done, Wi-Fi off");

    lib.scanLocalBooks(true);
    MemStats::log("sync: library rebuilt");   // the largest free block shows the fragmentation left
    MemStats::probeEngine("after sync");
    MemStats::logMap("after sync");
    if (lib.lastBook.filePath.length() == 0 && !lib.masterBooks.empty()) {
        lib.lastBook.filePath = lib.masterBooks[0].filePath;
        lib.lastBook.title = lib.masterBooks[0].title;
        lib.lastBook.author = lib.masterBooks[0].author;
        lib.lastBook.coverPath = lib.masterBooks[0].coverPath;
        lib.lastBook.fileId = lib.masterBooks[0].fileId;
        lib.lastBook.page = lib.masterBooks[0].page;
        lib.lastBook.totalPages = 1;
        lib.lastBook.progress = lib.masterBooks[0].progress;
        lib.lastBook.xpath = lib.masterBooks[0].xpath;
        lib.saveLastBook();
    }
    returnFromSync(ctx, prevState);
}
