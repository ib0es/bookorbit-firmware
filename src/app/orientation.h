#pragma once

// Orientations, a quarter turn a step: 0 portrait, 1 landscape (the panel's
// own), 2 portrait upside down, 3 landscape upside down. The interface turns
// by `ui` (Settings); a book's pages by `ui` and `book` together (Appearance:
// the book on top of the interface, e.g. both upside down = the book upright).
void applyOrientation(int ui, int book);

// The FreeInkUI orientation for one of the above.
#include <FreeInkUICore.h>
freeink::ui::Orientation uiOrientationOf(int orientation);

// A point of the interface (a tap, in its turned terms) -> the page's.
void interfaceToPage(int ui, int page, int16_t x, int16_t y, int16_t& px, int16_t& py);
