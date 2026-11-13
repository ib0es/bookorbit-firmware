#pragma once

// Every screen of the app in one table (A7): what handles its input, what
// draws it, which list selection a tap sets, and whether a vertical swipe
// pages its list. A new screen is a row here (plus its AppState), not edits
// to the controller's and the touch router's switches.

#include "app_state.h"
#include "../platform/hal/buttons.h"

struct ScreenEntry {
    AppState state;
    void (*handle)(AppContext& ctx, const ButtonEvent& event);   // nullptr: input ignored
    void (*draw)(AppContext& ctx, bool fullRefresh);            // nullptr: nothing to redraw
    int AppContext::* selection;   // set by a tap on a row, then OK; nullptr: taps handled elsewhere
    bool pagedList;               // a vertical swipe moves the selection a page
};

namespace Screens {
const ScreenEntry* find(AppState state);
}
