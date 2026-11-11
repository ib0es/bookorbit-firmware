#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

// Tap targets of the reader's menus (hit actions), for the touch router.
enum ReaderMenuAction : int {
    ActionChapter = 1, ActionMenuItem, ActionFontSize, ActionTypeface, ActionAppearance, ActionBookmark,
    ActionStatusBar,
    ActionValueLess,        // "-" of a value row (appearance, status bar): touch boards (C5)
    ActionValueMore,        // "+"
    ActionBookmarkDelete,   // "delete" beside a bookmark: touch boards (C1)
};

void drawReaderMenuScreen(AppContext& ctx, bool fullRefresh = false);
void drawTocScreen(AppContext& ctx, bool fullRefresh = false);
// Font size submenu: 12..32 pt, each row a sample in that size.
void drawFontMenuScreen(AppContext& ctx, bool fullRefresh = false);
// Book font families: the built-in one, then those in /fonts on the card.
void drawTypefaceMenuScreen(AppContext& ctx, bool fullRefresh = false);
// Typeface, size, spacing, margins... (src/app/screens/appearance.h), a sample below.
void drawAppearanceScreen(AppContext& ctx, bool fullRefresh = false);
void drawStatusBarScreen(AppContext& ctx, bool fullRefresh = false);
// This page's bookmark (add/remove) and the book's bookmarks.
void drawBookmarksScreen(AppContext& ctx, bool fullRefresh = false);
int bookmarkOnPage();   // index in Bookmarks of the page on screen's, -1 if none
// The families on the card the typeface menu lists (scanned again when `rescan`).
#include <vector>
#include "../../reader/font_store.h"
const std::vector<FontStore::Family>& typefaceFamilies(bool rescan);
// Sizes shown at once in the last drawn font size submenu (for swipe paging).
int fontMenuRowsVisible();
// Rows per page of the last drawn table of contents (for swipe paging).
int tocRowsPerPage();
