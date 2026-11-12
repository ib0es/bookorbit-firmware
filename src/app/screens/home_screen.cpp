#include "screens.h"
#include "../../platform/hal/storage.h"
#include "../../services/library_manager.h"
#include "../../services/power_service.h"
#include "../../services/sync_service.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/catalog_view.h"
#include "../../ui/views/library_view.h"
#include "../../ui/views/settings_view.h"
#include "../../ui/actions.h"

namespace {

enum HomeItem {
    HOME_RECENT_BOOK = 0,
    HOME_CATALOG,
    HOME_SETTINGS,
    HOME_SYNC_NOW,
};

void openLibrary(AppContext& ctx, LibraryFilter filter, bool rescan) {
    auto& lib = LibraryManager::getInstance();
    ctx.currentState = STATE_LIBRARY;
    lib.currentFolder = "/books";
    lib.currentFilter = filter;
    lib.selectedBookIndex = 0;
    if (rescan) lib.scanLocalBooks();
    else lib.rebuildFilteredIndices();
    drawLibraryScreen(ctx);
}

void cycleRecentBook(AppContext& ctx, int step) {
    auto& lib = LibraryManager::getInstance();
    std::vector<RecentBookInfo> recent = lib.getRecentBooks();
    if (recent.empty()) return;
    const int n = (int)recent.size();
    lib.recentBookIndex = ((lib.recentBookIndex + step) % n + n) % n;
    drawHomeScreen(ctx, false);
}

// Opens the featured recent book, downloading it first if it is only in the cloud.
void openRecentBook(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    std::vector<RecentBookInfo> recent = lib.getRecentBooks();
    if (recent.empty()) {
        openLibrary(ctx, FILTER_ALL, /*rescan=*/true);
        return;
    }
    if (lib.recentBookIndex >= (int)recent.size()) lib.recentBookIndex = 0;
    RecentBookInfo cur = recent[lib.recentBookIndex];

    if (!cur.isDownloaded || !Storage.exists(cur.filePath.c_str())) {
        for (auto& b : lib.masterBooks) {
            if (b.filePath.equalsIgnoreCase(cur.filePath) && b.downloadUrl().length() > 0) {
                if (!SyncService::downloadBook(ctx, b, STATE_HOME)) {
                    drawHomeScreen(ctx, false);
                    return;
                }
                cur.isDownloaded = b.isDownloaded;
                cur.coverPath = b.coverPath;
                cur.progress = b.progress;
                cur.page = b.page;
                cur.xpath = b.xpath;
                break;
            }
        }
    }

    ReaderScreen::openBook(ctx, cur.filePath, cur.title, cur.author, cur.coverPath, cur.progress,
                           cur.page, cur.xpath, cur.docId, cur.fileId);
}

void activateItem(AppContext& ctx, int index) {
    switch (index) {
        case HOME_RECENT_BOOK: openRecentBook(ctx); break;
        case HOME_CATALOG:
            // Books copied to / removed from the card since the last scan (USB, web
            // portal, a download): walked only when that may have happened (P3).
            LibraryManager::getInstance().refreshLocalFilesIfChanged();
            ctx.currentState = STATE_CATALOG;
            ctx.selectedCatalogIndex = 0;
            drawCatalogScreen(ctx, false);
            break;
        case HOME_SETTINGS:
            ctx.currentState = STATE_SETTINGS;
            ctx.selectedSettingIndex = 0;
            drawSettingsScreen(ctx, false);
            break;
        case HOME_SYNC_NOW:
            SyncService::syncWithBookOrbit(ctx);
            break;
    }
}

}  // namespace

namespace HomeScreen {

void handleEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const int sel = ctx.selectedHomeIndex;

    switch (Actions::of(event)) {
        case Action::Down:
            ctx.selectedHomeIndex = (sel + 1) % NUM_HOME_ITEMS;
            drawHomeScreen(ctx, false);
            break;
        case Action::Up:
            ctx.selectedHomeIndex = (sel - 1 + NUM_HOME_ITEMS) % NUM_HOME_ITEMS;
            drawHomeScreen(ctx, false);
            break;
        case Action::Next:
            if (sel == HOME_RECENT_BOOK) cycleRecentBook(ctx, +1);
            else activateItem(ctx, sel);
            break;
        case Action::Select:
            activateItem(ctx, sel);
            break;
        case Action::Prev:
            // On the recent book: the one before. Elsewhere LEFT goes back to the
            // book card, as in the other menus; it no longer starts a sync on
            // the "Sync now" row (U7).
            if (sel == HOME_RECENT_BOOK) {
                cycleRecentBook(ctx, -1);
            } else {
                ctx.selectedHomeIndex = HOME_RECENT_BOOK;
                drawHomeScreen(ctx, false);
            }
            break;
        case Action::Back:
            if (sel != HOME_RECENT_BOOK) {
                ctx.selectedHomeIndex = HOME_RECENT_BOOK;
                drawHomeScreen(ctx, false);
            } else {
                PowerService::enterDeepSleep(ctx);
            }
            break;
        default:
            break;
    }
}

}  // namespace HomeScreen
