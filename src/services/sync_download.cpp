#include "sync_service.h"
#include "sync_internal.h"
#include "../platform/hal/mem_stats.h"
#include "../platform/hal/background_task.h"
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

namespace {
struct DownloadJob {
    const AppConfig& config;
    const String& url;
    const String& path;
    std::atomic<int> percent{0};
    std::atomic<bool> cancel{false};
    bool ok = false;  // published by BackgroundTask completion
};

// Only one modal download runs at a time. The pointer is installed before the
// worker starts and cleared after join(); callbacks never touch UI or input.
DownloadJob* s_downloadJob = nullptr;

void publishDownloadPercent(int percent) { s_downloadJob->percent.store(percent, std::memory_order_relaxed); }

bool downloadCancelRequested() { return s_downloadJob->cancel.load(std::memory_order_relaxed); }

void runDownload(void* context) {
    auto& job = *static_cast<DownloadJob*>(context);
    job.ok = BookOrbit.downloadBook(job.config.serverHost, job.url, job.path, job.config.bookOrbitUser,
                                    job.config.bookOrbitPass, job.config.authToken, publishDownloadPercent,
                                    downloadCancelRequested);
}
}  // namespace

// ============================================================================
// Download
// ============================================================================
bool SyncService::downloadBook(AppContext& ctx, BookItem& book, AppState returnState) {
    KeepToken keepToken(ctx);
    const uint32_t prepareStarted = millis();
    WifiSession::prepare();
    Serial.printf("[NetPerf] download reader preparation: %lu ms\n", (unsigned long)(millis() - prepareStarted));
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
    const uint32_t authStarted = millis();
    ensureToken(ctx);
    Serial.printf("[NetPerf] download authentication: %lu ms\n", (unsigned long)(millis() - authStarted));
    if (cancelled()) return false;

    s_lastProgressTime = millis();
    s_lastProgressPercent = 0;
    s_downloadingBookTitle = utf8Prefix(book.title, 64);

    DownloadJob job{ctx.config, url, book.filePath};
    Platform::BackgroundTask downloadTask;
    s_downloadJob = &job;
    bool ok;
    if (downloadTask.start(runDownload, &job)) {
        uint32_t progressUiMs = 0;
        while (!downloadTask.finished()) {
            if (checkSyncCancel()) job.cancel.store(true, std::memory_order_relaxed);
            if (!s_syncCancelled) {
                const uint32_t uiStarted = millis();
                reportDownloadProgress(job.percent.load(std::memory_order_relaxed));
                progressUiMs += millis() - uiStarted;
            }
            // Match the old download sink's 100 ms cancel polling cadence:
            // touch/I2C scans at 100 Hz would compete with the network worker.
            delay(100);
        }
        // Do not touch files, start postprocessing or switch Wi-Fi off while
        // the worker still owns the HTTP client and the .part file.
        downloadTask.join();
        Serial.printf("[NetPerf] download progress UI: %lu ms (concurrent with transfer)\n",
                      (unsigned long)progressUiMs);
        ok = job.ok;
    } else {
        Serial.println("[Sync] Download task unavailable; downloading without progress redraws.");
        ok = BookOrbit.downloadBook(ctx.config.serverHost, url, book.filePath, ctx.config.bookOrbitUser,
                                    ctx.config.bookOrbitPass, ctx.config.authToken, nullptr, checkSyncCancel);
    }
    s_downloadJob = nullptr;
    if (cancelled()) return false;

    const uint32_t postStarted = millis();
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

    Serial.printf("[NetPerf] download postprocessing (cover+progress+bookmarks): %lu ms\n",
                  (unsigned long)(millis() - postStarted));
    BookOrbit.disconnectWiFi();
    if (cancelled()) return false;
    if (!ok) {
        Ui::status(tr(MsgId::COMMON_ERROR), tr(MsgId::SYNC_DOWNLOAD_ERR_L1), tr(MsgId::SYNC_DOWNLOAD_ERR_L2));
        PowerService::waitOrKey(2000);
        return false;
    }
    return true;
}
