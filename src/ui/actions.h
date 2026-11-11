#pragma once

// What a key means, in one place. Screens act on these instead of on key ids,
// so a board's keys (platform/devices/*.h) and the screen's orientation (the
// key remap, app/input) are settled before a screen sees the event.
//
// Keys only: taps and swipes carry a place or a direction and each screen
// handles them first (a tap usually ends as one of these, e.g. Select).

#include "../platform/hal/buttons.h"

enum class Action : uint8_t {
    None,
    Up, Down,       // move in a list (reading: the chapter before / after)
    Select,         // OK
    Back,
    Less, More,     // a value row: one step down / up (LEFT / RIGHT)
    Prev, Next,     // the page or item before / after (LEFT / RIGHT)
    Menu,           // the screen's menu (a held BACK / Home key)
    Delete,
};

namespace Actions {

// A clicked key: UP/DOWN -> Up/Down, OK -> Select, BACK -> Back, LEFT/RIGHT ->
// Prev/Next; a held BACK -> Menu. Anything else (taps, swipes, POWER, other
// long presses) -> None.
Action of(const ButtonEvent& e);

// On a value row LEFT/RIGHT step the value.
inline Action onValue(Action a) { return a == Action::Prev ? Action::Less : a == Action::Next ? Action::More : a; }

// Reading: where UP/DOWN turn pages (boards without LEFT/RIGHT, or the reader's
// own setting) they are Prev/Next; otherwise Up/Down jump chapters.
Action reading(const ButtonEvent& e, bool upDownTurnPagesSetting);

// A row with a menu (a book in the library): boards without a LEFT key open
// the menu with OK, as a tap does; folders still open directly.
template <typename RowHasMenu>
Action onRow(const ButtonEvent& e, RowHasMenu rowHasMenu);

bool okOpensRowMenu();

template <typename RowHasMenu>
Action onRow(const ButtonEvent& e, RowHasMenu rowHasMenu) {
    const Action a = of(e);
    return a == Action::Select && okOpensRowMenu() && rowHasMenu() ? Action::Menu : a;
}

}  // namespace Actions
