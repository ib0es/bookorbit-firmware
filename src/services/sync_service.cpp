#include "sync_service.h"
#include "../hal/mem_stats.h"
#include "../ui/modal.h"
#include "../net/bookorbit_client.h"
#include "../net/book_formats.h"
#include "../hal/buttons.h"
#include "../hal/storage.h"
#include "../ui/views/status_modal.h"
#include "../ui/views/home_view.h"
#include "../ui/views/library_view.h"
#include "../ui/views/settings_view.h"
#include "../ui/i18n.h"
#include "../reader/reader.h"
#include "progress_sync.h"
#include "sync_clock.h"

bool SyncService::s_syncCancelled = false;
uint32_t SyncService::s_lastProgressTime = 0;
int SyncService::s_lastProgressPercent = -1;
String SyncService::s_downloadingBookTitle = "";

// BACK held now, or a BACK click that arrived since the last poll, cancels the
// running network operation. Other pending clicks are dropped (screens are busy).
bool SyncService::checkSyncCancel() {
    if (s_syncCancelled) return true;
    Buttons.update();
    bool back = Buttons.isPressed(BTN_BACK);
    ButtonEvent evt;
    // A tap cancels too: touch-only boards (Picco) have no BACK key.
    while (!back && Buttons.getEvent(evt)) back = (evt.id == BTN_BACK || evt.id == BTN_TAP);
    if (back) {
        Buttons.clearEvents();
        s_syncCancelled = true;
    }
    return back;
}

void SyncService::abortSync(AppContext& ctx, AppState returnState, const char* title) {
    Serial.println("[Sync] Operation aborted by user.");
    BookOrbit.disconnectWiFi();
    drawStatusModal(title ? title : tr(MsgId::SYNC_ABORTED_TITLE), tr(MsgId::SYNC_ABORTED_USER), tr(MsgId::SYNC_ABORTED_BY_USER_L2), tr(MsgId::SYNC_ABORT_DISCONNECTING), tr(MsgId::SYNC_ABORT_RETURNING));
    delay(1200);
    Buttons.clearEvents();
    s_syncCancelled = false;
    showState(ctx, returnState);
}

// Goes back to `state` after a sync step and redraws it (reading -> the open page).
void SyncService::showState(AppContext& ctx, AppState state) {
    if (state == STATE_READING || state == STATE_READER_MENU) {
        state = Reader.isOpened() ? STATE_READING : STATE_HOME;
    }
    ctx.currentState = state;
    Modal.redrawScreen(false);
}

void SyncService::reportDownloadProgress(int percent) {
    uint32_t now = millis();
    // Only update display every 20% or at least 2 seconds apart, preventing E-ink flicker
    if ((percent - s_lastProgressPercent >= 20 || (now - s_lastProgressTime >= 2000 && percent > s_lastProgressPercent)) && percent < 100) {
        s_lastProgressPercent = percent;
        s_lastProgressTime = now;

        char pctText[48];
        snprintf(pctText, sizeof(pctText), tr(MsgId::SYNC_DOWNLOADING_FMT), percent);

        char bar[32];
        int filled = (percent * 14) / 100;
        int p = 0;
        bar[p++] = '[';
        for (int i = 0; i < 14; i++) {
            bar[p++] = (i < filled) ? '=' : (i == filled ? '>' : ' ');
        }
        bar[p++] = ']';
        bar[p] = '\0';

        drawStatusModal(tr(MsgId::LIB_HINT_DOWNLOAD_BOOK), s_downloadingBookTitle.c_str(), pctText, bar, nullptr, tr(MsgId::COMMON_CANCEL));
    }
}

// ============================================================================
// Helpers
// ============================================================================
namespace {

using ProgressSync::Action;

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

// Fills in missing server ids of a record from the library list / cached catalog.
// Exact matches only (server id, path, or full title).
void resolveServerIds(BookProgressRecord& rec) {
    auto& lib = LibraryManager::getInstance();
    for (const auto& b : lib.masterBooks) {
        if ((rec.fileId > 0 && b.fileId == rec.fileId) || b.filePath.equalsIgnoreCase(rec.filePath)) {
            if (rec.fileId <= 0 && b.fileId > 0) rec.fileId = b.fileId;
            if (rec.bookId <= 0 && b.bookId > 0) rec.bookId = b.bookId;
        }
    }
    if (rec.fileId > 0 && rec.bookId > 0) return;
    std::vector<BookMetadata> catalog;
    if (!Storage.loadCatalog(catalog)) return;
    for (const auto& c : catalog) {
        // The format of the record: its file id, or the path it is downloaded to.
        const BookFormatEntry* fmt = nullptr;
        for (const auto& f : c.formats) {
            if ((rec.fileId > 0 && f.fileId == rec.fileId) ||
                LibraryManager::catalogFormatPath(c, f).equalsIgnoreCase(rec.filePath)) { fmt = &f; break; }
        }
        const bool byTitle = !fmt && rec.title.length() > 0 && c.title.equalsIgnoreCase(rec.title) &&
                             BookFormats::fromPath(rec.filePath.c_str()) ==
                                 (BookFormats::Kind)(c.preferredFormat() ? c.preferredFormat()->kind : 0);
        if (fmt || byTitle) {
            if (rec.fileId <= 0) rec.fileId = fmt ? fmt->fileId : c.fileId;
            if (rec.bookId <= 0) rec.bookId = LibraryManager::catalogBookId(c);
            break;
        }
    }
}

bool isOpenInReader(const BookProgressRecord& rec) {
    return Reader.isOpened() && Reader.getOriginalFilePath().equalsIgnoreCase(rec.filePath);
}

}  // namespace

// ============================================================================
// Per-book progress
// ============================================================================
bool SyncService::syncBookProgress(AppContext& ctx, BookProgressRecord& rec, ServerProgress* serverOut) {
    auto& lib = LibraryManager::getInstance();
    auto& store = ProgressStore::instance();
    resolveServerIds(rec);
    // Progress is read by BOOK id. The file id is a different number space: using it
    // instead could fetch another book's position.
    const int fetchId = rec.bookId;
    if (fetchId <= 0) {
        Serial.printf("[Sync] '%s': book id unknown yet (file %d), skipping progress sync.\n", rec.title.c_str(), rec.fileId);
        return false;
    }

    // The open book: make sure the record carries the current page and CFI.
    if (isOpenInReader(rec)) lib.flushReaderPosition();

    // 1. Ask the server.
    ServerProgress srv;
    if (!BookOrbit.fetchBookProgress(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass,
                                     ctx.config.authToken, fetchId, rec.fileId, srv)) {
        Serial.printf("[Sync] '%s': could not read server progress.\n", rec.title.c_str());
        return false;
    }
    ProgressSync::Remote remote;
    remote.exists = srv.exists;
    remote.pos.percent = srv.percent;
    remote.pos.page = srv.page;
    remote.pos.cfi = srv.cfi.c_str();
    remote.updatedAt = srv.updatedAt;

    if (serverOut) {
        serverOut->exists = remote.exists;
        serverOut->percent = remote.pos.percent;
        serverOut->page = remote.pos.page;
        serverOut->cfi = remote.pos.cfi.c_str();
        serverOut->updatedAt = remote.updatedAt;
    }

    // 2. Decide.
    const ProgressSync::Decision d = ProgressSync::decide(rec.state, remote);
    Serial.printf("[Sync] '%s': local %.1f%%%s (changed %lu) | base %s%.1f%% | server %s%.1f%% (upd %lu) -> %s: %s\n",
                  rec.title.c_str(), rec.state.local.percent * 100.0f, rec.state.localDirty ? " dirty" : "",
                  (unsigned long)rec.state.localChangedAt, rec.state.hasBase ? "" : "none ",
                  rec.state.base.percent * 100.0f, remote.exists ? "" : "none ", remote.pos.percent * 100.0f,
                  (unsigned long)remote.updatedAt, ProgressSync::actionName(d.action), d.reason);

    // 3. Act.
    bool ok = true;
    switch (d.action) {
        case Action::Pull:
            ProgressSync::applyPull(rec.state, remote);
            if (isOpenInReader(rec)) {
                Reader.goToPosition(rec.state.local.cfi.c_str(), rec.state.local.xpath.c_str(),
                                    rec.state.local.percent, rec.state.local.page);
                rec.state.local.page = Reader.getCurrentPage();
            }
            break;

        case Action::Push: {
            if (rec.fileId <= 0) {
                Serial.println("[Sync] No file id, cannot upload progress.");
                ok = false;
                break;
            }
            // The server replaces the whole position with each write: without the CFI
            // the web reader would lose its place.
            uint32_t serverTs = 0;
            ok = BookOrbit.updateBookProgress(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass,
                                              ctx.config.authToken, rec.fileId, rec.state.local.percent,
                                              rec.state.local.page, rec.state.local.cfi.c_str(), &serverTs);
            if (ok) ProgressSync::applyPush(rec.state, serverTs ? serverTs : SyncClock::nowUtc());
            break;
        }

        case Action::None:
            ProgressSync::applyConverged(rec.state, remote);
            break;
    }

    store.touch();
    lib.applyRecordToLibrary(rec);
    lib.saveLastBook();   // writes last_book.json and progress.json
    return ok;
}

// ============================================================================
// Download
// ============================================================================
bool SyncService::downloadBook(AppContext& ctx, BookItem& book, AppState returnState) {
    const String url = book.downloadUrl();
    if (url.length() == 0) {
        drawStatusModal(tr(MsgId::COMMON_ERROR), tr(MsgId::SYNC_ERR_FILE_NOT_FOUND), "");
        delay(2000);
        return false;
    }

    s_syncCancelled = false;
    auto cancelled = [&]() {
        if (!(s_syncCancelled || checkSyncCancel())) return false;
        abortSync(ctx, returnState, tr(MsgId::SYNC_DOWNLOAD_ABORTED));
        return true;
    };
    drawStatusModal(tr(MsgId::SYNC_DOWNLOAD_BOOK_TITLE), book.title.substring(0, 36).c_str(), tr(MsgId::SYNC_CONNECTING_WIFI),
                    tr(MsgId::COMMON_WAIT), nullptr, tr(MsgId::COMMON_CANCEL));

    const bool wifi = BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 15000, checkSyncCancel);
    if (cancelled()) return false;
    if (!wifi) {
        drawStatusModal(tr(MsgId::WIFI_ERR_TITLE), tr(MsgId::WIFI_TEST_ERR_L1), tr(MsgId::WIFI_ERR_CHECK_SETTINGS));
        delay(2000);
        return false;
    }

    drawStatusModal(tr(MsgId::SYNC_DOWNLOAD_BOOK_TITLE), book.title.substring(0, 36).c_str(), tr(MsgId::SYNC_DOWNLOADING_FROM_SERVER),
                    ctx.config.serverHost.c_str(), nullptr, tr(MsgId::COMMON_CANCEL));
    ensureToken(ctx);
    if (cancelled()) return false;

    s_lastProgressTime = millis();
    s_lastProgressPercent = 0;
    s_downloadingBookTitle = book.title.substring(0, 32);

    bool ok = BookOrbit.downloadBook(ctx.config.serverHost, url, book.filePath, ctx.config.bookOrbitUser,
                                     ctx.config.bookOrbitPass, ctx.config.authToken, reportDownloadProgress, checkSyncCancel);

    if (ok) {
        book.isDownloaded = true;
        if (book.activeFormat >= 0 && book.activeFormat < (int)book.formats.size()) book.formats[book.activeFormat].onCard = true;
        Serial.printf("[Sync] Book %s downloaded successfully!\n", book.filePath.c_str());
        if (book.coverPath.length() == 0) {
            book.coverPath = LibraryManager::resolveCoverPath(book.filePath, book.title);
            if (book.coverPath.length() == 0) {
                book.coverPath = "/covers/" + LibraryManager::safeFileName(book.title, String(book.fileId)) + ".jpg";
            }
        }
        if (book.coverUrl.length() > 0 && !LibraryManager::isValidCoverFile(book.coverPath)) {
            BookOrbit.downloadCover(ctx.config.serverHost, book.coverUrl, book.coverPath, ctx.config.bookOrbitUser,
                                    ctx.config.bookOrbitPass, ctx.config.authToken, checkSyncCancel);
        }
        book.coverPath = LibraryManager::resolveCoverPath(book.filePath, book.title, book.coverPath);

        // Initialise this device's position from the server while Wi-Fi is up.
        BookProgressRecord& rec = ProgressStore::instance().getOrCreate(book.fileId, book.bookId, book.filePath, book.title);
        syncBookProgress(ctx, rec);
        book.progress = rec.state.local.percent;
        book.page = rec.state.local.page > 0 ? rec.state.local.page : 1;
        book.xpath = rec.state.local.xpath.c_str();
    }

    BookOrbit.disconnectWiFi();
    if (cancelled()) return false;
    if (!ok) {
        drawStatusModal(tr(MsgId::COMMON_ERROR), tr(MsgId::SYNC_DOWNLOAD_ERR_L1), tr(MsgId::SYNC_DOWNLOAD_ERR_L2));
        delay(2000);
        return false;
    }
    return true;
}

// ============================================================================
// Full sync
// ============================================================================
namespace {

void returnFromSync(AppContext& ctx, AppState prevState) {
    SyncService::showState(ctx, prevState);
}

void showConfigError(AppContext& ctx, AppState prevState, const char* what, const char* line1, const char* line2) {
    Serial.printf("[Sync] Not configured: %s\n", what);
    drawStatusModal(tr(MsgId::SYNC_CONFIG_ERR_TITLE), line1, line2);
    delay(2000);
    SyncService::showState(ctx, prevState);
}

// Record for a format of a catalog entry: by file id or the path it is downloaded
// to, then (preferred format only) by exact title for books sideloaded before they
// had a server id.
BookProgressRecord* recordForCatalogFormat(const BookMetadata& b, const BookFormatEntry& f) {
    ProgressStore& store = ProgressStore::instance();
    const String path = LibraryManager::catalogFormatPath(b, f);
    BookProgressRecord* rec = store.find(f.fileId, path);
    if (!rec && f.fileId == b.fileId) {
        for (auto& r : store.records()) {
            if (r.fileId <= 0 && r.title.equalsIgnoreCase(b.title) && r.filePath.equalsIgnoreCase(path)) { rec = &r; break; }
        }
        for (auto& r : store.records()) {
            if (rec) break;
            if (r.fileId <= 0 && r.title.equalsIgnoreCase(b.title) &&
                BookFormats::fromPath(r.filePath.c_str()) == (BookFormats::Kind)f.kind) rec = &r;
        }
    }
    if (rec) {
        if (f.fileId > 0) rec->fileId = f.fileId;
        const int bookId = LibraryManager::catalogBookId(b);
        if (bookId > 0) rec->bookId = bookId;
    }
    return rec;
}

// Result of syncing one opened book, reused for its catalog entry.
struct SyncedBook {
    int fileId = 0;
    int bookId = 0;
    ServerProgress server;
};

// By file id: each format of a book is synced on its own.
const SyncedBook* findSynced(const std::vector<SyncedBook>& synced, int fileId) {
    for (const auto& s : synced) {
        if (fileId > 0 && s.fileId == fileId) return &s;
    }
    return nullptr;
}

// Three-way sync of every book that has a progress record on this device.
void syncOpenedBooks(AppContext& ctx, std::vector<SyncedBook>& synced) {
    auto& records = ProgressStore::instance().records();
    for (size_t i = 0; i < records.size(); i++) {
        if (SyncService::isCancelled() || SyncService::checkSyncCancel()) break;
        BookProgressRecord& rec = records[i];
        if (rec.filePath.length() > 0 && !Storage.exists(rec.filePath.c_str())) continue;   // deleted book
        resolveServerIds(rec);
        if (rec.bookId <= 0) continue;   // matched against the catalog below
        SyncedBook s;
        SyncService::syncBookProgress(ctx, rec, &s.server);   // a failure leaves it queued (dirty)
        s.fileId = rec.fileId;
        s.bookId = rec.bookId;
        if (s.fileId > 0 || s.bookId > 0) synced.push_back(s);   // do not ask again for its catalog entry
    }
    ProgressStore::instance().save();
}

// Adds saved catalog entries missing from a truncated fresh catalog.
void mergeSavedCatalog(std::vector<BookMetadata>& catalog) {
    std::vector<BookMetadata> saved;
    if (!Storage.loadCatalog(saved)) return;
    size_t added = 0;
    for (const auto& old : saved) {
        bool present = false;
        for (const auto& b : catalog) {
            if ((old.fileId > 0 && b.fileId == old.fileId) || (old.id.length() > 0 && b.id == old.id)) { present = true; break; }
        }
        if (!present) { catalog.push_back(old); added++; }
    }
    Serial.printf("[Sync] Catalog was truncated: kept %u books from the saved catalog.\n", (unsigned)added);
}

void downloadCovers(AppContext& ctx, const std::vector<BookMetadata>& catalog) {
    auto& lib = LibraryManager::getInstance();
    int downloaded = 0;
    for (const auto& b : catalog) {
        if (SyncService::isCancelled() || SyncService::checkSyncCancel()) break;
        if (b.coverUrl.length() == 0) continue;

        const String safe = LibraryManager::safeFileName(b.title, b.id);
        const String coverPath = "/covers/" + safe + ".jpg";
        const String destPath = LibraryManager::catalogBookPath(b);
        const bool wanted = lib.lastBook.filePath.equalsIgnoreCase(destPath) ||
                            Storage.exists(destPath.c_str()) || downloaded < 4;
        if (wanted && !LibraryManager::isValidCoverFile(coverPath)) {
            Serial.printf("[Sync] Downloading cover thumbnail: %s -> %s\n", b.title.c_str(), coverPath.c_str());
            if (BookOrbit.downloadCover(ctx.config.serverHost, b.coverUrl, coverPath, ctx.config.bookOrbitUser,
                                        ctx.config.bookOrbitPass, ctx.config.authToken, SyncService::checkSyncCancel)) {
                downloaded++;
            }
        }
    }
}

}  // namespace

void SyncService::syncWithBookOrbit(AppContext& ctx) {
    AppState prevState = ctx.currentState;
    s_syncCancelled = false;
    ctx.currentState = STATE_SYNCING;

    // Debounce / wait for release of any buttons pressed when entering sync
    uint32_t waitStart = millis();
    while ((Buttons.isPressed(BTN_CONFIRM) || Buttons.isPressed(BTN_RIGHT) || Buttons.isPressed(BTN_BACK)) && (millis() - waitStart < 250)) {
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

    drawStatusModal(tr(MsgId::SYNC_MODAL_TITLE), tr(MsgId::SYNC_CONNECTING_WIFI_PREFIX), ctx.config.wifiSsid.c_str(), nullptr, nullptr, tr(MsgId::COMMON_CANCEL));
    if (!BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 10000, checkSyncCancel)) {
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }
        Serial.println("[Sync] Wi-Fi connection failed.");
        drawStatusModal(tr(MsgId::WIFI_ERR_TITLE), tr(MsgId::SYNC_ERR_WIFI_CONNECT_L1), tr(MsgId::SYNC_ERR_WIFI_CONNECT_L2));
        delay(1800);
        lib.scanLocalBooks();
        returnFromSync(ctx, prevState);
        return;
    }
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    MemStats::log("sync: Wi-Fi up");
    drawStatusModal(tr(MsgId::SYNC_MODAL_TITLE), tr(MsgId::SYNC_WIFI_CONNECTED), tr(MsgId::SYNC_FETCHING_CATALOG_PROGRESS), ctx.config.serverHost.c_str(), nullptr, tr(MsgId::COMMON_CANCEL));
    ensureToken(ctx);
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    // 1. Reading progress of every book opened on this device, before the catalog:
    //    it must not depend on the catalog fitting into memory, and the heap is
    //    at its largest now.
    std::vector<SyncedBook> synced;
    syncOpenedBooks(ctx, synced);
    if (s_syncCancelled || checkSyncCancel()) {
        abortSync(ctx, prevState);
        return;
    }

    // 2. OPDS catalog (re-login once if the token went stale).
    std::vector<BookMetadata> catalog;
    bool catOk = BookOrbit.fetchCatalog(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass, ctx.config.authToken, catalog);
    if (!catOk && ctx.config.bookOrbitUser.length() > 0 && ctx.config.bookOrbitPass.length() > 0) {
        ctx.config.authToken = "";
        ensureToken(ctx);
        catOk = BookOrbit.fetchCatalog(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass, ctx.config.authToken, catalog);
    }

    if (catOk) {
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }
        Serial.printf("[Sync] Catalog fetched: %d books available.\n", (int)catalog.size());
        MemStats::log("sync: catalog parsed");
        const CatalogFetchInfo& ci = BookOrbit.lastCatalog;

        if (catalog.empty() && !ci.cleanEmpty()) {
            // Something went wrong on the device side (stream error, unrecognised
            // entries): keep the saved catalog and show why.
            Serial.printf("[Sync] Empty catalog looks wrong: http=%d err=%d bytes=%u skipped=%u heap=%u/%u truncated=%d\n",
                          ci.httpCode, ci.streamError, (unsigned)ci.bytes, (unsigned)ci.skipped,
                          (unsigned)ci.heapStart, (unsigned)ci.heapMin, (int)ci.truncated);
            char l1[64], l2[64];
            snprintf(l1, sizeof(l1), tr(MsgId::SYNC_CATALOG_PARSE_FMT1), ci.httpCode, (unsigned)ci.bytes, (unsigned)ci.skipped);
            snprintf(l2, sizeof(l2), tr(MsgId::SYNC_CATALOG_PARSE_FMT2), (unsigned)(ci.heapStart / 1024),
                     (unsigned)(ci.heapMin / 1024), ci.streamError);
            drawStatusModal(tr(MsgId::SYNC_CATALOG_PARSE_ERR_TITLE), l1, l2, tr(MsgId::SYNC_CATALOG_KEPT));
            delay(4000);
            catalog.clear();
            Storage.loadCatalog(catalog);
        } else if (ci.truncated) {
            // Out of memory part-way: keep the books we knew about but did not get now.
            mergeSavedCatalog(catalog);
        }

        // 3. Server progress for the catalog: from step 1 for opened books; books
        //    downloaded here but never opened get a record (first sync = pull);
        //    for the rest a snapshot, only to show it in the library.
        //    Each format separately: a book read here as FB2 and on the web as EPUB
        //    has two positions. The snapshot shown is the preferred format's.
        for (auto& b : catalog) {
            if (s_syncCancelled || checkSyncCancel()) break;
            ServerProgress snapshot;
            const int bookId = LibraryManager::catalogBookId(b);
            for (const auto& f : b.formats) {
                if (s_syncCancelled || checkSyncCancel()) break;
                const bool preferred = f.fileId == b.fileId;
                ServerProgress srv;
                const SyncedBook* done = findSynced(synced, f.fileId);
                if (done) {
                    srv = done->server;
                } else {
                    BookProgressRecord* rec = recordForCatalogFormat(b, f);
                    const String path = LibraryManager::catalogFormatPath(b, f);
                    if (!rec && (Storage.exists(path.c_str()) || lib.lastBook.filePath.equalsIgnoreCase(path))) {
                        rec = &ProgressStore::instance().getOrCreate(f.fileId, bookId, path, b.title);
                    }
                    if (rec) {
                        syncBookProgress(ctx, *rec, &srv);
                        SyncedBook s;
                        s.fileId = f.fileId;
                        s.bookId = bookId;
                        s.server = srv;
                        synced.push_back(s);
                    } else if (preferred && bookId > 0) {
                        BookOrbit.fetchBookProgress(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass,
                                                    ctx.config.authToken, bookId, f.fileId, srv);
                    }
                }
                if (preferred) snapshot = srv;
            }
            b.lastProgress = snapshot.exists ? snapshot.percent : 0.0f;
            b.lastPage = snapshot.page;
            b.serverUpdatedAt = snapshot.updatedAt;
        }
        Storage.saveCatalog(catalog);
        ProgressStore::instance().save();

        downloadCovers(ctx, catalog);
        if (s_syncCancelled || checkSyncCancel()) {
            abortSync(ctx, prevState);
            return;
        }

        char catInfo[64];
        snprintf(catInfo, sizeof(catInfo), tr(MsgId::SYNC_FOUND_BOOKS_FMT), (int)catalog.size());
        drawStatusModal(tr(MsgId::SYNC_SUCCESS_TITLE), catInfo, tr(MsgId::SYNC_CATALOG_UPDATED_L2));
        delay(800);
    } else {
        Serial.println("[Sync] Book Orbit server not reachable or catalog fetch failed!");
        drawStatusModal(tr(MsgId::SYNC_SERVER_UNREACHABLE_TITLE), tr(MsgId::SYNC_SERVER_UNREACHABLE_L1), tr(MsgId::SYNC_SERVER_UNREACHABLE_L2));
        delay(1800);
    }

    // The fetched catalog is on the card now: free it (and the per-book results)
    // before the book list is rebuilt, so the list's long-lived strings are not
    // allocated between short-lived ones (a fragmented heap after a sync had
    // only ~50 KB in one piece of ~110 KB free).
    std::vector<BookMetadata>().swap(catalog);
    std::vector<SyncedBook>().swap(synced);
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

// ============================================================================
// Current book only (closing the book, sleep, boot)
// ============================================================================
void SyncService::syncCurrentBookProgress(AppContext& ctx, bool autoConnectWiFi) {
    auto& lib = LibraryManager::getInstance();
    if (lib.lastBook.filePath.length() == 0) return;
    if (Reader.isOpened()) lib.flushReaderPosition();

    bool didConnectWiFi = false;
    if (!BookOrbit.isConnected()) {
        if (!autoConnectWiFi) return;
        if (ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0) return;
        Serial.println("[Sync] Auto-connecting Wi-Fi for progress sync...");
        if (!BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 8000)) {
            Serial.println("[Sync] Auto-connecting Wi-Fi failed; progress stays queued (dirty).");
            return;
        }
        didConnectWiFi = true;
    }

    ensureToken(ctx);
    syncBookProgress(ctx, lib.currentRecord());

    if (didConnectWiFi) BookOrbit.disconnectWiFi();
}

// ---- background upload after closing a book ----
namespace {
enum class BgState { Idle, Connecting };
BgState s_bgState = BgState::Idle;
int s_bgFileId = 0;
bool s_bgOnlyIfDirty = true;
String s_bgPath;
uint32_t s_bgStartedAt = 0;
constexpr uint32_t kBgConnectTimeoutMs = 15000;
}  // namespace

bool SyncService::backgroundActive() { return s_bgState != BgState::Idle; }

void SyncService::cancelBackground() {
    if (s_bgState == BgState::Idle) return;
    s_bgState = BgState::Idle;
    BookOrbit.setForegroundConnectHook(nullptr);
    Serial.println("[Sync] Background upload cancelled; progress stays queued (dirty).");
    // The foreground operation that cancelled us reconnects as it needs.
}

void SyncService::startBackgroundUpload(AppContext& ctx, int fileId, const String& filePath, bool onlyIfDirty) {
    if (ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0) return;
    if (s_bgState != BgState::Idle) cancelBackground();
    s_bgFileId = fileId;
    s_bgPath = filePath;
    s_bgOnlyIfDirty = onlyIfDirty;
    s_bgStartedAt = millis();
    s_bgState = BgState::Connecting;
    BookOrbit.setForegroundConnectHook(&SyncService::cancelBackground);
    BookOrbit.beginWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str());
    Serial.printf("[Sync] Background %s queued for %s\n", onlyIfDirty ? "upload" : "sync", filePath.c_str());
}

void SyncService::tickBackground(AppContext& ctx) {
    if (s_bgState != BgState::Connecting) return;
    if (!BookOrbit.isConnected()) {
        if (millis() - s_bgStartedAt > kBgConnectTimeoutMs) {
            s_bgState = BgState::Idle;
            BookOrbit.setForegroundConnectHook(nullptr);
            BookOrbit.disconnectWiFi();
            Serial.println("[Sync] Background upload: no Wi-Fi; progress stays queued (dirty).");
        }
        return;
    }
    // Connected: one short exchange (a request or two), then the radio goes off.
    s_bgState = BgState::Idle;
    BookOrbit.setForegroundConnectHook(nullptr);
    const uint32_t t0 = millis();
    BookProgressRecord* rec = ProgressStore::instance().find(s_bgFileId, s_bgPath);
    if (rec && (rec->state.localDirty || !s_bgOnlyIfDirty)) {
        const int pageBefore = Reader.isOpened() ? Reader.getCurrentPage() : 0;
        ensureToken(ctx);
        const bool ok = syncBookProgress(ctx, *rec);
        Serial.printf("[Sync] Background sync %s in %lu ms (Wi-Fi took %lu ms)\n", ok ? "done" : "failed",
                      (unsigned long)(millis() - t0), (unsigned long)(t0 - s_bgStartedAt));
        // The server had a newer position for the open book: show that page.
        if (ctx.currentState == STATE_READING && Reader.isOpened() && Reader.getCurrentPage() != pageBefore) {
            Reader.renderCurrentPage();
        }
    }
    BookOrbit.disconnectWiFi();
}

SyncService::BookSyncResult SyncService::syncBookItem(AppContext& ctx, BookItem& book) {
    if (ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0) return BookSyncResult::NotConfigured;
    auto& lib = LibraryManager::getInstance();
    const bool isCurrent = lib.lastBook.filePath.equalsIgnoreCase(book.filePath);
    if (isCurrent && Reader.isOpened()) lib.flushReaderPosition();

    drawStatusModal(tr(MsgId::BOOK_SYNC_TITLE), book.title.substring(0, 32).c_str(), tr(MsgId::BOOK_SYNC_CONNECTING),
                    nullptr, nullptr, tr(MsgId::BOOK_SYNC_CANCEL));
    s_syncCancelled = false;
    Buttons.clearEvents();

    bool ok = false;
    const bool wasConnected = BookOrbit.isConnected();
    if (wasConnected || BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 8000, checkSyncCancel)) {
        if (!s_syncCancelled && !checkSyncCancel()) {
            ensureToken(ctx);
            BookProgressRecord& rec = isCurrent ? lib.currentRecord()
                : ProgressStore::instance().getOrCreate(book.fileId, book.bookId, book.filePath, book.title);
            ok = syncBookProgress(ctx, rec);
            // A book never opened here keeps showing the server's position
            // (from the catalog) unless the sync produced one.
            if (ProgressSync::hasProgress(rec.state.local.percent)) {
                book.progress = rec.state.local.percent;
                if (rec.state.local.page > 0) book.page = rec.state.local.page;
                book.xpath = rec.state.local.xpath.c_str();
            }
        }
        if (!wasConnected) BookOrbit.disconnectWiFi();
    }
    Buttons.clearEvents();
    if (s_syncCancelled) return BookSyncResult::Cancelled;
    return ok ? BookSyncResult::Synced : BookSyncResult::Failed;
}

void SyncService::showBookSyncResult(BookSyncResult r, const BookItem& book) {
    Serial.printf("[Sync] Book menu sync of '%s': result %d\n", book.title.c_str(), (int)r);
    switch (r) {
        case BookSyncResult::Synced: {
            char line[48];
            snprintf(line, sizeof(line), tr(MsgId::BOOK_SYNC_PROGRESS_FMT), book.progress * 100.0f);
            Modal.showMessage(tr(MsgId::BOOK_SYNC_DONE), book.title.substring(0, 32), line);
            break;
        }
        case BookSyncResult::Failed:
            Modal.showMessage(tr(MsgId::BOOK_SYNC_FAILED), tr(MsgId::BOOK_SYNC_FAILED_L1), tr(MsgId::BOOK_SYNC_FAILED_L2));
            break;
        case BookSyncResult::NotConfigured:
            Modal.showMessage(tr(MsgId::BOOK_SYNC_FAILED), tr(MsgId::BOOK_SYNC_NO_SETUP_L1), tr(MsgId::BOOK_SYNC_NO_SETUP_L2));
            break;
        case BookSyncResult::Cancelled:
            break;
    }
}

void SyncService::syncOnBootProgress(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    if (!ctx.config.syncOnBoot || ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0 || lib.lastBook.filePath.length() == 0) {
        return;
    }

    Serial.println("[Boot] syncOnBoot enabled: synchronizing progress with server...");
    drawStatusModal(tr(MsgId::SYNC_BOOT_TITLE), tr(MsgId::SYNC_BOOT_CHECKING), lib.lastBook.title.substring(0, 32).c_str(), nullptr, nullptr, tr(MsgId::COMMON_CANCEL));

    s_syncCancelled = false;
    uint32_t waitStart = millis();
    while (Buttons.isPressed(BTN_BACK) && (millis() - waitStart < 200)) {
        Buttons.update();
        delay(10);
    }
    Buttons.clearEvents();

    if (BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 8000, checkSyncCancel)) {
        if (!s_syncCancelled && !checkSyncCancel()) {
            ensureToken(ctx);
            syncBookProgress(ctx, lib.currentRecord());
        }
        BookOrbit.disconnectWiFi();
    }
}
