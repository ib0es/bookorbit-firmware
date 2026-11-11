#pragma once

#include <Arduino.h>
#include "../app/app_state.h"
#include "library_manager.h"
#include "../storage/progress_store.h"
#include "../net/bookorbit_client.h"

// Network operations with Book Orbit: catalog, downloads and reading progress.
//
// Progress for every book goes through syncBookProgress(): fetch the server
// position, ProgressSync::decide() (three-way merge against the last agreed
// position, see progress_sync.h), then pull / push / nothing. All entry points
// (full sync, reader menu, closing a book, sleep, boot, download) use it.
class SyncService {
public:
    static bool checkSyncCancel();
    static void showState(AppContext& ctx, AppState state);
    static void abortSync(AppContext& ctx, AppState returnState = STATE_HOME, const char* title = nullptr);
    static void reportDownloadProgress(int percent);

    // Downloads book file, initialises its progress from the server and caches the cover.
    static bool downloadBook(AppContext& ctx, BookItem& book, AppState returnState);

    // Full synchronization: Wi-Fi, OPDS catalog, progress of every book opened here, covers.
    static void syncWithBookOrbit(AppContext& ctx);

    // Progress of the current book (lastBook) only.
    static void syncCurrentBookProgress(AppContext& ctx, bool autoConnectWiFi = false);

    // Background sync of one book: Wi-Fi connects while the user goes on, and
    // tickBackground() (every loop) reconciles the book with the server once
    // connected, then turns the radio off. Used after closing a book (only an
    // unsynced position is sent: onlyIfDirty) and after waking up (full pull /
    // push; a newer server position moves the open page). Any foreground
    // network operation or sleep cancels it; local progress then stays queued.
    static void startBackgroundUpload(AppContext& ctx, int fileId, const String& filePath, bool onlyIfDirty = true);
    static void tickBackground(AppContext& ctx);
    static void cancelBackground();
    static bool backgroundActive();

    // Progress of one library book (book menu): connects Wi-Fi with a status
    // screen, reconciles the book with the server and updates `book`. The
    // caller redraws its screen, then calls showBookSyncResult().
    enum class BookSyncResult { Synced, Failed, NotConfigured, Cancelled };
    static BookSyncResult syncBookItem(AppContext& ctx, BookItem& book);
    static void showBookSyncResult(BookSyncResult r, const BookItem& book);

    // Boot-time progress check if syncOnBoot is active
    static void syncOnBootProgress(AppContext& ctx);

    // Reconciles one book with the server. Requires Wi-Fi. If `serverOut` is given it
    // receives what the server reported (for the catalog snapshot). Returns false if
    // the server could not be asked or an upload failed.
    static bool syncBookProgress(AppContext& ctx, BookProgressRecord& rec, ServerProgress* serverOut = nullptr);

    static bool isCancelled() { return s_syncCancelled; }
    static void resetCancel() { s_syncCancelled = false; }

private:
    static bool s_syncCancelled;
    static uint32_t s_lastProgressTime;
    static int s_lastProgressPercent;
    static String s_downloadingBookTitle;
};
