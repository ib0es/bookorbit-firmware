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
            if (!cfiApplies(rec)) {
                // The web reader's locator for an FB2 (or other non-EPUB) file is not a
                // CFI of the EPUB the reader made from it: only the percent carries over.
                rec.state.local.cfi.clear();
                rec.state.local.xpath.clear();
            }
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
                                              rec.state.local.page,
                                              cfiApplies(rec) ? rec.state.local.cfi.c_str() : "", &serverTs);
            if (ok) ProgressSync::applyPush(rec.state, serverTs ? serverTs : SyncClock::nowUtc());
            break;
        }

        case Action::None:
            ProgressSync::applyConverged(rec.state, remote);
            break;
    }

    store.touch();
    lib.applyRecordToLibrary(rec);
    if (!s_batch) lib.saveLastBook();   // writes last_book.json and progress.json (a full sync: once at its end)
    syncBookmarks(ctx, rec);
    return ok;
}

// ============================================================================
// Current book only (closing the book, sleep, boot)
// ============================================================================
void SyncService::syncCurrentBookProgress(AppContext& ctx, bool autoConnectWiFi) {
    KeepToken keepToken(ctx);
    WifiSession::prepare();
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

SyncService::BookSyncResult SyncService::syncBookItem(AppContext& ctx, BookItem& book) {
    KeepToken keepToken(ctx);
    WifiSession::prepare();
    if (ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0) return BookSyncResult::NotConfigured;
    auto& lib = LibraryManager::getInstance();
    const bool isCurrent = lib.lastBook.filePath.equalsIgnoreCase(book.filePath);
    if (isCurrent && Reader.isOpened()) lib.flushReaderPosition();

    Ui::status(tr(MsgId::BOOK_SYNC_TITLE), utf8Prefix(book.title, 64).c_str(), tr(MsgId::BOOK_SYNC_CONNECTING),
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
            Ui::message(tr(MsgId::BOOK_SYNC_DONE), utf8Prefix(book.title, 64).c_str(), line);
            break;
        }
        case BookSyncResult::Failed:
            Ui::message(tr(MsgId::BOOK_SYNC_FAILED), tr(MsgId::BOOK_SYNC_FAILED_L1), tr(MsgId::BOOK_SYNC_FAILED_L2));
            break;
        case BookSyncResult::NotConfigured:
            Ui::message(tr(MsgId::BOOK_SYNC_FAILED), tr(MsgId::BOOK_SYNC_NO_SETUP_L1), tr(MsgId::BOOK_SYNC_NO_SETUP_L2));
            break;
        case BookSyncResult::Cancelled:
            break;
    }
}

void SyncService::syncOnBootProgress(AppContext& ctx) {
    KeepToken keepToken(ctx);
    WifiSession::prepare();
    auto& lib = LibraryManager::getInstance();
    if (!ctx.config.syncOnBoot || ctx.config.wifiSsid.length() == 0 || ctx.config.serverHost.length() == 0 || lib.lastBook.filePath.length() == 0) {
        return;
    }

    Serial.println("[Boot] syncOnBoot enabled: synchronizing progress with server...");
    Ui::status(tr(MsgId::SYNC_BOOT_TITLE), tr(MsgId::SYNC_BOOT_CHECKING), utf8Prefix(lib.lastBook.title, 64).c_str(), nullptr, nullptr, tr(MsgId::COMMON_CANCEL));

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
