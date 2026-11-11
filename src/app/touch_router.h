#pragma once

// Touch support for touchscreen boards (X4 Pro, Picco).
// Library: a tap on a book opens the book menu, a tap on a folder opens it.
//
// Screens register a hit area per tappable element while they draw (UiKit
// Frame::hit with an action and a value, typically the row index). A tap is
// turned into what a button user would do: select that element, then CONFIRM.
// The reading screen has no elements; it is split into tap zones instead
// (left third = previous page, right third = next page, middle = book menu).

#include "app_state.h"
#include "../platform/hal/buttons.h"

namespace TouchRouter {

// Translates a BTN_TAP into the button event the current screen understands,
// updating the screen's selection first. Returns false when the tap hit nothing.
bool translate(AppContext& ctx, const ButtonEvent& tap, ButtonEvent& out);

// Swipes. Reading: left = next page, right = previous page. Elsewhere a swipe
// to the right is BACK; in the library and the table of contents an upward
// swipe shows the next page of the list and a downward one the previous page
// (redrawn here, so it returns false). Returns false when nothing applies.
bool translateSwipe(AppContext& ctx, const ButtonEvent& swipe, ButtonEvent& out);

}  // namespace TouchRouter
