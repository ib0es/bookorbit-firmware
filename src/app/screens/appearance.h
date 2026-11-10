#pragma once

// The Appearance menu's rows: what each shows and how it changes. The view
// (reader_menu_view.cpp) draws them, the reader screen steps them; the
// settings live in AppConfig and reach the reader when the menu is left
// (one layout for any number of changes).

#include <Arduino.h>

#include "../app_state.h"
#include "../../reader/book_reader.h"

namespace Appearance {

enum Row : int {
    TYPEFACE,       // opens the typeface list
    SIZE,           // steps, or opens the size list
    LINE_SPACING,
    PARAGRAPH_SPACING,
    MARGINS,
    ALIGNMENT,
    HYPHENATION,
    KEEP_LINES,
    BOOK_STYLES,
    FOCUS_READING,
    FULL_REFRESH,
    COUNT
};

const char* label(int row);
// The value as shown (in `buf`).
const char* value(const AppConfig& cfg, int row, char* buf, size_t len);
// One step up (dir > 0) or down; false for rows that open a list instead.
bool step(AppConfig& cfg, int row, int dir);

BookReader::LayoutOptions layoutOptions(const AppConfig& cfg);

}  // namespace Appearance
