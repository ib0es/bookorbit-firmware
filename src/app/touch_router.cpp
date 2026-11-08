#include "touch_router.h"
#include "../services/library_manager.h"
#include "../ui/kit/ui_kit.h"
#include "screens/screens.h"
#include "../reader/reader.h"
#include "../ui/views/library_view.h"
#include "../ui/views/reader_menu_view.h"
#include "../ui/font.h"

namespace TouchRouter {

namespace {

ButtonEvent make(ButtonId id) {
    ButtonEvent e{ id, ACTION_CLICK };
    return e;
}

// Reading: three vertical zones.
ButtonEvent readingZone(const ButtonEvent& tap) {
    const int16_t w = uikit::width();
    if (tap.x < w / 3) return make(BTN_LEFT);
    if (tap.x >= w - w / 3) return make(BTN_RIGHT);
    return make(BTN_CONFIRM);
}

}  // namespace

bool translate(AppContext& ctx, const ButtonEvent& tap, ButtonEvent& out) {
    if (ctx.currentState == STATE_READING) {
        out = readingZone(tap);
        return true;
    }

    // Title bar = BACK (Picco has no BACK key).
    if (uikit::backTapped(tap.x, tap.y)) {
        out = make(BTN_BACK);
        return true;
    }

    freeink::ui::ActionId action = 0;
    int16_t value = 0;
    if (!uikit::hitTest(tap.x, tap.y, action, value)) return false;

    switch (ctx.currentState) {
        case STATE_HOME:
            // home_view: 1 = continue-reading card, 2 = category (1=catalog, 2=settings), 3 = sync row
            ctx.selectedHomeIndex = (action == 1) ? 0 : (action == 3 ? 3 : value);
            break;
        case STATE_CATALOG:
            ctx.selectedCatalogIndex = value;
            break;
        case STATE_LIBRARY: {
            auto& lib = LibraryManager::getInstance();
            if (value < 0 || value >= lib.rowCount()) return false;
            lib.selectedBookIndex = value;
            // A tap on a book opens its menu (Read first); folders open directly.
            if (!lib.rowAt(value).isDirectory) {
                LibraryScreen::openBookMenu(ctx);
                return false;
            }
            break;
        }
        case STATE_SETTINGS:          ctx.selectedSettingIndex = value; break;
        case STATE_WIFI_SETTINGS:     ctx.selectedWifiSettingIndex = value; break;
        case STATE_SERVER_SETTINGS:   ctx.selectedServerSettingIndex = value; break;
        case STATE_LANGUAGE_SETTINGS: ctx.selectedLanguageIndex = value; break;
        case STATE_READER_MENU:       ctx.selectedReaderMenuIndex = value; break;
        case STATE_TOC:               ctx.selectedTocIndex = value; break;
        case STATE_BOOK_MENU:         ctx.selectedBookMenuIndex = value; break;
        case STATE_FONT_MENU:         ctx.selectedFontIndex = value; break;
        case STATE_SYNC_SETTINGS:     ctx.selectedSyncSettingIndex = value; break;
        case STATE_SLEEP_COVER_SETTINGS: ctx.selectedSleepCoverIndex = value; break;
        case STATE_FRONTLIGHT_SETTINGS:  // sliders handle the tap themselves
            SettingsScreen::handleFrontlightTap(ctx, action, value, tap.x);
            return false;
        default:
            return false;
    }
    out = make(BTN_CONFIRM);
    return true;
}

namespace {

// Moves a paged list's selection by one page: to the first row of the
// next/previous page. Returns true if it moved.
bool pageSelection(int& index, int count, int perPage, bool forward) {
    if (count <= 0 || perPage <= 0) return false;
    const int page = index / perPage;
    const int lastPage = (count - 1) / perPage;
    const int target = forward ? page + 1 : page - 1;
    if (target < 0 || target > lastPage) return false;
    index = target * perPage;
    return true;
}

}  // namespace

bool translateSwipe(AppContext& ctx, const ButtonEvent& swipe, ButtonEvent& out) {
    const bool vertical = swipe.id == BTN_SWIPE_UP || swipe.id == BTN_SWIPE_DOWN;
    const bool forward = swipe.id == BTN_SWIPE_UP;  // finger up = further down the list

    if (ctx.currentState == STATE_READING) {
        if (vertical) return false;
        out = make(swipe.id == BTN_SWIPE_LEFT ? BTN_RIGHT : BTN_LEFT);
        return true;
    }

    if (ctx.currentState == STATE_HOME) {
        // Home: horizontal swipes step through the recent books (LEFT/RIGHT on the card).
        if (vertical) return false;
        ctx.selectedHomeIndex = 0;
        out = make(swipe.id == BTN_SWIPE_LEFT ? BTN_RIGHT : BTN_LEFT);
        return true;
    }
    if (swipe.id == BTN_SWIPE_RIGHT) {
        out = make(BTN_BACK);
        return true;
    }
    if (!vertical) return false;

    if (ctx.currentState == STATE_LIBRARY) {
        auto& lib = LibraryManager::getInstance();
        int idx = lib.selectedBookIndex;
        if (pageSelection(idx, lib.rowCount(), libraryRowsPerPage(), forward)) {
            lib.selectedBookIndex = idx;
            drawLibraryScreen(ctx);
        }
        return false;
    }
    if (ctx.currentState == STATE_FONT_MENU) {
        // Up: further down the list of sizes (larger); down: back up.
        const int n = FontEngine::kSizeCount, step = fontMenuRowsVisible();
        int idx = ctx.selectedFontIndex + (forward ? step : -step);
        idx = idx < 0 ? 0 : (idx >= n ? n - 1 : idx);
        if (idx != ctx.selectedFontIndex) {
            ctx.selectedFontIndex = idx;
            drawFontMenuScreen(ctx, false);
        }
        return false;
    }
    if (ctx.currentState == STATE_TOC) {
        int idx = ctx.selectedTocIndex;
        if (pageSelection(idx, Reader.getChapterCount(), tocRowsPerPage(), forward)) {
            ctx.selectedTocIndex = idx;
            drawTocScreen(ctx, true);
        }
        return false;
    }
    return false;
}

}  // namespace TouchRouter
