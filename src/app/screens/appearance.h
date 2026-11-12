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
    IMAGES,
    ORIENTATION,
    FULL_REFRESH,
    UPDOWN_KEYS,    // UP / DOWN while reading: chapters or pages
    STATUS_BAR,     // opens the status bar menu
    AUTO_ROTATE,    // boards with a motion sensor only: the last row (U3)
    COUNT
};

// The book's Appearance menu: the rows of the page itself (typeface to
// orientation). The device-wide ones (status bar, full refresh, pictures,
// UP/DOWN keys) live in Settings -> Reading; auto-rotate in Settings ->
// Screen orientation.
int count();
int rowAt(int index);   // the Row shown at `index`

// Settings -> Reading: the reading rows that are not about one book's page.
namespace Reading {
int count();
int rowAt(int index);
}  // namespace Reading

const char* label(int row);
// The value as shown (in `buf`).
const char* value(const AppConfig& cfg, int row, char* buf, size_t len);
// One step up (dir > 0) or down; false for rows that open a list instead.
bool step(AppConfig& cfg, int row, int dir);

BookReader::LayoutOptions layoutOptions(const AppConfig& cfg);

}  // namespace Appearance
