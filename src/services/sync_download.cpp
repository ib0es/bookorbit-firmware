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
// Download
// ============================================================================
bool SyncService::downloadBook(AppContext& ctx, BookItem& book, AppState returnState) {
    KeepToken keepToken(ctx);
    WifiSession::prepare();
    const String url = book.downloadUrl();
    if (url.length() == 0) {
        Ui::status(tr(MsgId::COMMON_ERROR), tr(MsgId::SYNC_ERR_FILE_NOT_FOUND), "");
        PowerService::waitOrKey(2000);
        return false;
    }

    s_syncCancelled = false;
    auto cancelled = [&]() {
        if (!(s_syncCancelled || checkSyncCancel())) return false;
        abortSync(ctx, returnState, tr(MsgId::SYNC_DOWNLOAD_ABORTED));
        return true;
    };
    Ui::status(tr(MsgId::SYNC_DOWNLOAD_BOOK_TITLE), utf8Prefix(book.title, 64).c_str(), tr(MsgId::SYNC_CONNECTING_WIFI),
               tr(MsgId::COMMON_WAIT), nullptr, tr(MsgId::COMMON_CANCEL));

    const bool wifi =
            BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 15000, checkSyncCancel);
    if (cancelled()) return false;
    if (!wifi) {
        Ui::status(tr(MsgId::WIFI_ERR_TITLE), tr(MsgId::WIFI_TEST_ERR_L1),
                   wifiFailureLine(BookOrbit.lastWifiFailure(), tr(MsgId::WIFI_ERR_CHECK_SETTINGS)));
        PowerService::waitOrKey(2000);
        return false;
    }

    Ui::status(tr(MsgId::SYNC_DOWNLOAD_BOOK_TITLE), utf8Prefix(book.title, 64).c_str(),
               tr(MsgId::SYNC_DOWNLOADING_FROM_SERVER), ctx.config.serverHost.c_str(), nullptr,
               tr(MsgId::COMMON_CANCEL));
    ensureToken(ctx);
    if (cancelled()) return false;

    s_lastProgressTime = millis();
    s_lastProgressPercent = 0;
    s_downloadingBookTitle = utf8Prefix(book.title, 64);

    bool ok = BookOrbit.downloadBook(ctx.config.serverHost, url, book.filePath, ctx.config.bookOrbitUser,
                                     ctx.config.bookOrbitPass, ctx.config.authToken, reportDownloadProgress,
                                     checkSyncCancel);

    if (ok) {
        book.isDownloaded = true;
        if (book.activeFormat >= 0 && book.activeFormat < (int)book.formats.size())
            book.formats[book.activeFormat].onCard = true;
        Serial.printf("[Sync] Book %s downloaded successfully!\n", book.filePath.c_str());
        LibraryManager::getInstance().cardMayHaveChanged();
        if (book.coverPath.length() == 0) {
            book.coverPath = LibraryManager::resolveCoverPath(book.filePath, book.title);
            if (book.coverPath.length() == 0) {
                book.coverPath = "/covers/" + LibraryManager::catalogStem(book.title, book.bookId) + ".jpg";
            }
        }
        if (book.coverUrl.length() > 0 && !LibraryManager::isValidCoverFile(book.coverPath) &&
            !LibraryManager::coverKnownUnusable(book.coverPath, book.coverUrl)) {
            if (BookOrbit.downloadCover(ctx.config.serverHost, book.coverUrl, book.coverPath, ctx.config.bookOrbitUser,
                                        ctx.config.bookOrbitPass, ctx.config.authToken, checkSyncCancel) &&
                !LibraryManager::isValidCoverFile(book.coverPath)) {
                LibraryManager::markCoverUnusable(book.coverPath, book.coverUrl);
            }
        }
        book.coverPath = LibraryManager::resolveCoverPath(book.filePath, book.title, book.coverPath);

        // Initialise this device's position from the server while Wi-Fi is up.
        BookProgressRecord& rec =
                ProgressStore::instance().getOrCreate(book.fileId, book.bookId, book.filePath, book.title);
        syncBookProgress(ctx, rec);
        book.progress = rec.state.local.percent;
        book.page = rec.state.local.page > 0 ? rec.state.local.page : 1;
        book.xpath = rec.state.local.xpath.c_str();
    }

    BookOrbit.disconnectWiFi();
    if (cancelled()) return false;
    if (!ok) {
        Ui::status(tr(MsgId::COMMON_ERROR), tr(MsgId::SYNC_DOWNLOAD_ERR_L1), tr(MsgId::SYNC_DOWNLOAD_ERR_L2));
        PowerService::waitOrKey(2000);
        return false;
    }
    return true;
}
