#include "screens.h"
#include "../../platform/hal/storage.h"
#include "../../services/library_manager.h"
#include "../../services/power_service.h"
#include "../../services/sync_service.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/catalog_view.h"
#include "../../ui/views/library_view.h"
#include "../../ui/views/settings_view.h"

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
            // Books copied to / removed from the card since the last scan (USB, web portal).
            LibraryManager::getInstance().refreshLocalFiles();
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

    switch (event.id) {
        case BTN_DOWN:
            ctx.selectedHomeIndex = (sel + 1) % NUM_HOME_ITEMS;
            drawHomeScreen(ctx, false);
            break;
        case BTN_UP:
            ctx.selectedHomeIndex = (sel - 1 + NUM_HOME_ITEMS) % NUM_HOME_ITEMS;
            drawHomeScreen(ctx, false);
            break;
        case BTN_RIGHT:
            if (sel == HOME_RECENT_BOOK) cycleRecentBook(ctx, +1);
            else activateItem(ctx, sel);
            break;
        case BTN_CONFIRM:
            activateItem(ctx, sel);
            break;
        case BTN_LEFT:
            if (sel == HOME_RECENT_BOOK) cycleRecentBook(ctx, -1);
            else if (sel == HOME_SYNC_NOW) SyncService::syncWithBookOrbit(ctx);
            break;
        case BTN_BACK:
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
