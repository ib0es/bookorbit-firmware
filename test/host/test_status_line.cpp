#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include "core/status_line.h"

static std::string side(bool pages, bool percent, const char* numbers, int pct) {
    if (pages && percent) return std::string(numbers) + " (" + std::to_string(pct) + "%)";
    if (pages) return numbers;
    return percent ? std::to_string(pct) + "%" : "";
}

int main() {
    static_assert(SB_TOP_DEFAULT == (SB_BOOK_TITLE | SB_BATTERY | SB_CLOCK));
    static_assert(SB_BOTTOM_DEFAULT == (SB_CH_PAGES | SB_CH_PERCENT | SB_CH_BAR | SB_CH_TITLE));
    static_assert(SB_TOP_ALL == SB_BOTTOM_ALL && NUM_SB_TOP_ITEMS == 11 && NUM_SB_BOTTOM_ITEMS == 11);
    const StatusLine::Where where{2, 12, 3, 18, 12, 340, 17, 4};
    for (int mask = 0; mask <= SB_ALL; ++mask) {
        char left[64], right[64], center[256];
        StatusLine::sides(mask, where, "12:34", left, sizeof(left), right, sizeof(right));
        StatusLine::center(mask, where, "Book", "Chapter", center, sizeof(center));
        assert(left == side(mask & SB_CH_PAGES, mask & SB_CH_PERCENT, "3/18", 17));
        std::string expectedRight = side(mask & SB_BOOK_PAGES, mask & SB_BOOK_PERCENT, "12/340", 4);
        if (mask & SB_CLOCK) expectedRight += (expectedRight.empty() ? "" : "  |  ") + std::string("12:34");
        assert(right == expectedRight);
        std::string expectedCenter;
        if (mask & SB_BOOK_TITLE) expectedCenter = "Book";
        if (mask & SB_CH_NUMBER) expectedCenter += (expectedCenter.empty() ? "" : " · ") + std::string("2/12");
        if (mask & SB_CH_TITLE) expectedCenter += (expectedCenter.empty() ? "" : " · ") + std::string("Chapter");
        assert(center == expectedCenter);
        const bool leftOn = mask & (SB_CH_PAGES | SB_CH_PERCENT);
        const bool rightOn = mask & (SB_BOOK_PAGES | SB_BOOK_PERCENT | SB_BATTERY | SB_CLOCK);
        const bool centerOn = mask & (SB_BOOK_TITLE | SB_CH_TITLE | SB_CH_NUMBER);
        const bool wrapped = leftOn && rightOn && centerOn;
        assert(StatusLine::centerAbove(mask) == wrapped);
        const int lines = wrapped ? 2 : (leftOn || rightOn || centerOn) ? 1 : 0;
        assert(StatusLine::textLines(mask) == lines);
        assert(StatusLine::room(mask) == (lines == 2                           ? 3
                                          : lines == 1                         ? 2
                                          : (mask & (SB_CH_BAR | SB_BOOK_BAR)) ? 1
                                                                               : 0));
        // No clock text before RTC/server time is available, even when enabled.
        StatusLine::sides(mask, where, "", left, sizeof(left), right, sizeof(right));
        assert(right == side(mask & SB_BOOK_PAGES, mask & SB_BOOK_PERCENT, "12/340", 4));
        StatusLine::sides(mask, where, nullptr, left, sizeof(left), right, sizeof(right));
        assert(right == side(mask & SB_BOOK_PAGES, mask & SB_BOOK_PERCENT, "12/340", 4));
        // Empty metadata and unknown page totals never produce bogus 0/0 or separators.
        const StatusLine::Where unknown{};
        StatusLine::center(mask, unknown, "", nullptr, center, sizeof(center));
        assert(!*center);
        StatusLine::sides(mask, unknown, "", left, sizeof(left), right, sizeof(right));
        assert(left == std::string((mask & SB_CH_PERCENT) ? "0%" : ""));
        assert(right == std::string((mask & SB_BOOK_PERCENT) ? "0%" : ""));
        // Truncation must terminate each buffer without touching its neighbours.
        struct Small {
            char before = 'A';
            char text[2]{};
            char after = 'Z';
        } a, b, c;
        StatusLine::sides(mask, where, "12:34", a.text, sizeof(a.text), b.text, sizeof(b.text));
        StatusLine::center(mask, where, "Длинное название книги", "Название главы", c.text, sizeof(c.text));
        for (const Small* s : {&a, &b, &c}) {
            assert(s->before == 'A' && s->after == 'Z');
            assert(std::memchr(s->text, 0, sizeof(s->text)));
        }
    }
    std::cout << "StatusLine: all 2048 masks, defaults, wrapping, missing metadata/time and bounded buffers passed\n";
}
