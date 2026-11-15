#pragma once

// Status bar lines (above and below the book text): which items they show
// (bits, kept in AppConfig::sbTopItems and sbBottomItems) and their text layout.
// Pure logic: the reader draws it, Settings previews it.
//
// Both bars share the same items and layout rules (the top bar is the vertical
// mirror of the bottom bar):
// Sides: Left: chapter (page, percent); Right: book (page, percent, clock, battery).
// Center: book title, chapter number, chapter title.
// When left, right, and center are all active, center wraps to a line of its own
// (closer to the book text).
// Progress bars: chapter bar, book bar (on the outer screen edges).

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum SbItem : int {
    SB_CH_PAGES = 1,        // page in the chapter, "3/18"
    SB_CH_BAR = 2,          // chapter progress line
    SB_BOOK_BAR = 4,        // book progress line
    SB_CH_TITLE = 8,        // chapter title
    SB_CH_PERCENT = 16,     // part of the chapter read
    SB_BOOK_PERCENT = 32,   // part of the book read
    SB_CH_NUMBER = 64,      // chapter number, "2/12"
    SB_BOOK_PAGES = 128,    // page in the book, "12/340"
    SB_BOOK_TITLE = 256,    // book title
    SB_BATTERY = 512,       // battery icon and percentage
    SB_CLOCK = 1024,        // clock, "12:00"
    SB_ALL = 2047
};
const int NUM_SB_ITEMS = 11;
const int NUM_SB_BOTTOM_ITEMS = NUM_SB_ITEMS;
const int NUM_SB_TOP_ITEMS = NUM_SB_ITEMS;

const int SB_BARS = SB_CH_BAR | SB_BOOK_BAR;
const int SB_BOTTOM_BARS = SB_BARS;
const int SB_TOP_BARS = SB_BARS;

const int SB_BOTTOM_ALL = SB_ALL;
const int SB_TOP_ALL = SB_ALL;

const int SB_BOTTOM_LEGACY_FULL = SB_CH_PAGES | SB_BOOK_BAR | SB_CH_TITLE | SB_BOOK_PERCENT;
const int SB_BOTTOM_DEFAULT = SB_CH_PAGES | SB_CH_PERCENT | SB_CH_BAR | SB_CH_TITLE;
const int SB_TOP_DEFAULT = SB_BOOK_TITLE | SB_BATTERY | SB_CLOCK;

// Compatibility aliases
using SbBottomItem = SbItem;
using SbTopItem = SbItem;

const int SB_BOTTOM_CH_PAGES = SB_CH_PAGES;
const int SB_BOTTOM_CH_BAR = SB_CH_BAR;
const int SB_BOTTOM_BOOK_BAR = SB_BOOK_BAR;
const int SB_BOTTOM_CH_TITLE = SB_CH_TITLE;
const int SB_BOTTOM_CH_PERCENT = SB_CH_PERCENT;
const int SB_BOTTOM_BOOK_PERCENT = SB_BOOK_PERCENT;
const int SB_BOTTOM_CH_NUMBER = SB_CH_NUMBER;
const int SB_BOTTOM_BOOK_PAGES = SB_BOOK_PAGES;
const int SB_BOTTOM_BOOK_TITLE = SB_BOOK_TITLE;
const int SB_BOTTOM_BATTERY = SB_BATTERY;
const int SB_BOTTOM_CLOCK = SB_CLOCK;

const int SB_TOP_CH_PAGES = SB_CH_PAGES;
const int SB_TOP_CH_BAR = SB_CH_BAR;
const int SB_TOP_BOOK_BAR = SB_BOOK_BAR;
const int SB_TOP_CH_TITLE = SB_CH_TITLE;
const int SB_TOP_CH_PERCENT = SB_CH_PERCENT;
const int SB_TOP_BOOK_PERCENT = SB_BOOK_PERCENT;
const int SB_TOP_CH_NUMBER = SB_CH_NUMBER;
const int SB_TOP_BOOK_PAGES = SB_BOOK_PAGES;
const int SB_TOP_BOOK_TITLE = SB_BOOK_TITLE;
const int SB_TOP_BATTERY = SB_BATTERY;
const int SB_TOP_CLOCK = SB_CLOCK;

namespace StatusLine {

struct Where {
    int chapter, chapters;   // 1-based chapter number and their count (0: unknown)
    int chPage, chPages;     // page in the chapter
    int page, pages;         // page in the book
    int chPercent, percent;  // read, 0..100
};

// The three parts of the line.
const int SB_LEFT = SB_CH_PAGES | SB_CH_PERCENT;
const int SB_RIGHT = SB_BOOK_PAGES | SB_BOOK_PERCENT | SB_BATTERY | SB_CLOCK;
const int SB_CENTER = SB_CH_TITLE | SB_CH_NUMBER | SB_BOOK_TITLE;

const int SB_BOTTOM_LEFT = SB_LEFT;
const int SB_BOTTOM_RIGHT = SB_RIGHT;
const int SB_BOTTOM_CENTER = SB_CENTER;

const int SB_TOP_LEFT = SB_LEFT;
const int SB_TOP_RIGHT = SB_RIGHT;
const int SB_TOP_CENTER = SB_CENTER;

// With both sides on there is little room between them: the center items
// get a line of their own (wrapped to the next line).
inline bool centerAbove(int items) { return (items & SB_LEFT) && (items & SB_RIGHT) && (items & SB_CENTER); }

// Lines of text needed: 0, 1, or 2 (center on a line of its own).
inline int textLines(int items) {
    if (!(items & (SB_LEFT | SB_RIGHT | SB_CENTER))) return 0;
    return centerAbove(items) ? 2 : 1;
}

// Room needed: 3 (two lines of text), 2 (one line of text), 1 (bars only), 0 (nothing).
inline int room(int items) {
    const int lines = textLines(items);
    return lines == 2 ? 3 : lines == 1 ? 2 : (items & SB_BARS) ? 1 : 0;
}

// The sides' text. Left, the chapter: its page and percent, "3/18 (17%)", or
// the one of them that is on. Right, the book: "12/340 (4%)", "12/340", "4%", and clock.
inline void sides(int items, const Where& w, const char* clk, char* left, size_t leftLen, char* right,
                  size_t rightLen) {
    left[0] = right[0] = 0;
    auto side = [](char* buf, size_t len, const char* numbers, bool showPercent, int percent) {
        if (*numbers && showPercent)
            snprintf(buf, len, "%s (%d%%)", numbers, percent);
        else if (*numbers)
            snprintf(buf, len, "%s", numbers);
        else if (showPercent)
            snprintf(buf, len, "%d%%", percent);
    };
    char nums[24] = "";
    if ((items & SB_CH_PAGES) && w.chPages > 0) snprintf(nums, sizeof(nums), "%d/%d", w.chPage, w.chPages);
    side(left, leftLen, nums, items & SB_CH_PERCENT, w.chPercent);

    char bookNums[24] = "";
    char bookSide[32] = "";
    if ((items & SB_BOOK_PAGES) && w.pages > 0) snprintf(bookNums, sizeof(bookNums), "%d/%d", w.page, w.pages);
    side(bookSide, sizeof(bookSide), bookNums, items & SB_BOOK_PERCENT, w.percent);

    const bool hasClock = (items & SB_CLOCK) && clk && *clk;
    if (*bookSide && hasClock)
        snprintf(right, rightLen, "%s  |  %s", bookSide, clk);
    else if (*bookSide)
        snprintf(right, rightLen, "%s", bookSide);
    else if (hasClock)
        snprintf(right, rightLen, "%s", clk);
}

// The centre: book title, chapter number and title.
inline void center(int items, const Where& w, const char* bookTitle, const char* chTitle, char* buf, size_t len) {
    buf[0] = 0;
    const bool number = (items & SB_CH_NUMBER) && w.chapters > 0;
    const bool name = (items & SB_CH_TITLE) && chTitle && *chTitle;
    const bool book = (items & SB_BOOK_TITLE) && bookTitle && *bookTitle;

    char ch[128] = "";
    if (number && name)
        snprintf(ch, sizeof(ch), "%d/%d \xC2\xB7 %s", w.chapter, w.chapters, chTitle);
    else if (number)
        snprintf(ch, sizeof(ch), "%d/%d", w.chapter, w.chapters);
    else if (name)
        snprintf(ch, sizeof(ch), "%s", chTitle);

    if (book && *ch)
        snprintf(buf, len, "%s \xC2\xB7 %s", bookTitle, ch);
    else if (book)
        snprintf(buf, len, "%s", bookTitle);
    else if (*ch)
        snprintf(buf, len, "%s", ch);
}

}  // namespace StatusLine
