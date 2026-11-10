#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

void drawReaderMenuScreen(AppContext& ctx, bool fullRefresh = false);
void drawTocScreen(AppContext& ctx, bool fullRefresh = false);
// Font size submenu: 12..32 pt, each row a sample in that size.
void drawFontMenuScreen(AppContext& ctx, bool fullRefresh = false);
// Book font families: the built-in one, then those in /fonts on the card.
void drawTypefaceMenuScreen(AppContext& ctx, bool fullRefresh = false);
// The families on the card the typeface menu lists (scanned again when `rescan`).
#include <vector>
#include "../../reader/font_store.h"
const std::vector<FontStore::Family>& typefaceFamilies(bool rescan);
// Sizes shown at once in the last drawn font size submenu (for swipe paging).
int fontMenuRowsVisible();
// Rows per page of the last drawn table of contents (for swipe paging).
int tocRowsPerPage();
