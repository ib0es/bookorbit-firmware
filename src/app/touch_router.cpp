#include "touch_router.h"
#include "screen_table.h"
#include "../services/library_manager.h"
#include "../ui/kit/ui_kit.h"
#include "screens/screens.h"
#include "../reader/reader.h"
#include "orientation.h"
#include "../platform/hal/display.h"
#include "../ui/views/library_view.h"
#include "../ui/views/reader_menu_view.h"
#include "../ui/font.h"
#include "../ui/modal.h"

namespace TouchRouter {

namespace {

ButtonEvent make(ButtonId id) {
    ButtonEvent e{ id, ACTION_CLICK };
    return e;
}

// Reading: three vertical zones of the page (taps come in the interface's
// terms; the book may be turned on top of it).
ButtonEvent readingZone(const AppContext& ctx, const ButtonEvent& tap) {
    int16_t x, y;
    interfaceToPage(ctx.config.uiOrientation, Reader.orientation(), tap.x, tap.y, x, y);
    const int16_t w = (int16_t)(Reader.landscape() ? EPD_WIDTH : EPD_HEIGHT);
    if (x < w / 3) return make(BTN_LEFT);
    if (x >= w - w / 3) return make(BTN_RIGHT);
    return make(BTN_CONFIRM);
}

}  // namespace

bool translate(AppContext& ctx, const ButtonEvent& tap, ButtonEvent& out) {
    if (ctx.currentState == STATE_READING) {
        out = readingZone(ctx, tap);
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

    // Value rows' "-" / "+" (C5) and a bookmark's "delete" (C1): the row is
    // selected and stepped down / up, or deleted, as LEFT / RIGHT would.
    if (ctx.currentState == STATE_APPEARANCE_MENU || ctx.currentState == STATE_STATUSBAR_MENU ||
        ctx.currentState == STATE_READING_SETTINGS) {
        if (action == ActionValueLess || action == ActionValueMore) {
            (ctx.currentState == STATE_APPEARANCE_MENU  ? ctx.selectedAppearanceIndex
             : ctx.currentState == STATE_STATUSBAR_MENU ? ctx.selectedStatusBarIndex
                                                        : ctx.selectedReadingSettingIndex) = value;
            out = make(action == ActionValueLess ? BTN_LEFT : BTN_RIGHT);
            return true;
        }
    }
    if (ctx.currentState == STATE_BOOKMARKS && action == ActionBookmarkDelete) {
        ctx.selectedBookmarkIndex = value;
        out = make(BTN_LEFT);
        return true;
    }

    switch (ctx.currentState) {
        case STATE_HOME:
            // home_view: 1 = continue-reading card, 2 = category (1=catalog, 2=settings), 3 = sync row
            ctx.selectedHomeIndex = (action == 1) ? 0 : (action == 3 ? 3 : value);
            break;
        case STATE_LIBRARY: {
            auto& lib = LibraryManager::getInstance();
            if (value < 0 || value >= lib.rowCount()) return false;
            lib.selectedBookIndex = value;
            // A tap on a book opens its menu (Read first); folders and format rows
            // (a book's list of formats: they have no menu) open directly.
            if (!lib.rowAt(value).isDirectory && lib.rowAt(value).formatRow < 0) {
                LibraryScreen::openBookMenu(ctx);
                return false;
            }
            break;
        }
        case STATE_USB_MSC:
            SettingsScreen::handleUsbMscTap(ctx, action);
            return false;
        case STATE_FRONTLIGHT_SETTINGS:  // sliders handle the tap themselves
            SettingsScreen::handleFrontlightTap(ctx, action, value, tap.x);
            return false;
        default: {
            // A list screen: the row tapped is selected, then OK (screen_table.cpp).
            const ScreenEntry* screen = Screens::find(ctx.currentState);
            if (!screen || !screen->selection) return false;
            ctx.*(screen->selection) = value;
            break;
        }
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

// The selection of the screens whose list pages through uikit::cardList /
// setListPager (settings, catalog, the menus).
int* pagedSelection(AppContext& ctx) {
    const ScreenEntry* screen = Screens::find(ctx.currentState);
    return screen && screen->pagedList && screen->selection ? &(ctx.*(screen->selection)) : nullptr;
}

}  // namespace

bool translateSwipe(AppContext& ctx, const ButtonEvent& swipe, ButtonEvent& out) {
    const bool vertical = swipe.id == BTN_SWIPE_UP || swipe.id == BTN_SWIPE_DOWN;
    const bool forward = swipe.id == BTN_SWIPE_UP;  // finger up = further down the list

    if (ctx.currentState == STATE_READING) {
        // The swipe in the page's terms: a finger moving left turns to the next page.
        const int sx = swipe.id == BTN_SWIPE_LEFT ? -1 : swipe.id == BTN_SWIPE_RIGHT ? 1 : 0;
        const int sy = swipe.id == BTN_SWIPE_UP ? -1 : swipe.id == BTN_SWIPE_DOWN ? 1 : 0;
        int16_t ax, ay, bx, by;
        interfaceToPage(ctx.config.uiOrientation, Reader.orientation(), 200, 200, ax, ay);
        interfaceToPage(ctx.config.uiOrientation, Reader.orientation(), (int16_t)(200 + sx), (int16_t)(200 + sy), bx, by);
        (void)vertical;
        if (bx == ax) return false;   // along the page's height
        out = make(bx < ax ? BTN_RIGHT : BTN_LEFT);
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
            drawTocScreen(ctx, uikit::periodicFullRefresh());
        }
        return false;
    }
    // Card lists and menus that did not fit on the screen (landscape): a page.
    const uikit::ListPager pager = uikit::listPager();
    if (int* sel = pagedSelection(ctx)) {
        if (pager.count > pager.perPage && pageSelection(*sel, pager.count, pager.perPage, forward))
            Modal.redrawScreen(false);
    }
    return false;
}

}  // namespace TouchRouter
