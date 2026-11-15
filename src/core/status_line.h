#pragma once

// The bottom line of a book page: which items it shows (bits, kept in
// AppConfig::sbBottomItems) and the text of its two sides. Pure logic: the
// reader draws it, Settings previews it.
//
// Left: the chapter (the page in it, its percent); right: the book (the page
// in it, its percent); between them which chapter (its number and title), on
// a line of its own above them when both sides are on; the progress lines under all.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum SbBottomItem : int {
    SB_BOTTOM_CH_PAGES = 1,        // page in the chapter, "3/18"
    SB_BOTTOM_CH_BAR = 2,          // chapter progress line
    SB_BOTTOM_BOOK_BAR = 4,        // book progress line
    SB_BOTTOM_CH_TITLE = 8,        // chapter title
    SB_BOTTOM_CH_PERCENT = 16,     // part of the chapter read
    SB_BOTTOM_BOOK_PERCENT = 32,   // part of the book read
    SB_BOTTOM_CH_NUMBER = 64,      // chapter number, "2/12"
    SB_BOTTOM_BOOK_PAGES = 128,    // page in the book, "12/340"
    SB_BOTTOM_ALL = 255
};
const int NUM_SB_BOTTOM_ITEMS = 8;
// The bars only: no line of text needed.
const int SB_BOTTOM_BARS = SB_BOTTOM_CH_BAR | SB_BOTTOM_BOOK_BAR;
// The old "chapter and bar": page in the chapter, chapter title, book percent, book bar.
const int SB_BOTTOM_DEFAULT = SB_BOTTOM_CH_PAGES | SB_BOTTOM_BOOK_BAR | SB_BOTTOM_CH_TITLE | SB_BOTTOM_BOOK_PERCENT;

namespace StatusLine {

struct Where {
    int chapter, chapters;   // 1-based chapter number and their count (0: unknown)
    int chPage, chPages;     // page in the chapter
    int page, pages;         // page in the book
    int chPercent, percent;  // read, 0..100
};

// The three parts of the line.
const int SB_BOTTOM_LEFT = SB_BOTTOM_CH_PAGES | SB_BOTTOM_CH_PERCENT;       // the chapter
const int SB_BOTTOM_RIGHT = SB_BOTTOM_BOOK_PAGES | SB_BOTTOM_BOOK_PERCENT;  // the book
const int SB_BOTTOM_CENTER = SB_BOTTOM_CH_TITLE | SB_BOTTOM_CH_NUMBER;      // which chapter

// With both sides on there is little room between them: the chapter's title
// and number get a line of their own, above.
inline bool centerAbove(int items) {
    return (items & SB_BOTTOM_LEFT) && (items & SB_BOTTOM_RIGHT) && (items & SB_BOTTOM_CENTER);
}

// Lines of text the bottom line needs: 0, 1, or 2 (the centre above the sides).
inline int textLines(int items) {
    if (!(items & (SB_BOTTOM_LEFT | SB_BOTTOM_RIGHT | SB_BOTTOM_CENTER))) return 0;
    return centerAbove(items) ? 2 : 1;
}

// The sides' text. Left, the chapter: its page and percent, "3/18 (17%)", or
// the one of them that is on. Right, the book: "12/340 (4%)", "12/340", "4%".
inline void sides(int items, const Where& w, char* left, size_t leftLen, char* right, size_t rightLen) {
    left[0] = right[0] = 0;
    // `numbers` and, when there are some, the percent in brackets after them.
    auto side = [](char* buf, size_t len, const char* numbers, bool showPercent, int percent) {
        if (*numbers && showPercent)
            snprintf(buf, len, "%s (%d%%)", numbers, percent);
        else if (*numbers)
            snprintf(buf, len, "%s", numbers);
        else if (showPercent)
            snprintf(buf, len, "%d%%", percent);
    };
    char nums[24] = "";
    if ((items & SB_BOTTOM_CH_PAGES) && w.chPages > 0) snprintf(nums, sizeof(nums), "%d/%d", w.chPage, w.chPages);
    side(left, leftLen, nums, items & SB_BOTTOM_CH_PERCENT, w.chPercent);
    nums[0] = 0;
    if ((items & SB_BOTTOM_BOOK_PAGES) && w.pages > 0) snprintf(nums, sizeof(nums), "%d/%d", w.page, w.pages);
    side(right, rightLen, nums, items & SB_BOTTOM_BOOK_PERCENT, w.percent);
}

// The centre: which chapter, its number and title, "2/12 · Down the Rabbit-Hole".
inline void center(int items, const Where& w, const char* title, char* buf, size_t len) {
    buf[0] = 0;
    const bool number = (items & SB_BOTTOM_CH_NUMBER) && w.chapters > 0;
    const bool name = (items & SB_BOTTOM_CH_TITLE) && title && *title;
    if (number && name)
        snprintf(buf, len, "%d/%d \xC2\xB7 %s", w.chapter, w.chapters, title);
    else if (number)
        snprintf(buf, len, "%d/%d", w.chapter, w.chapters);
    else if (name)
        snprintf(buf, len, "%s", title);
}

}  // namespace StatusLine
