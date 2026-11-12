#pragma once

#include "../platform/hal/buttons.h"

struct AppContext;

// From keys and touches to what the screens get. Installed once at start-up:
// - the touch mapper (panel coordinates -> logical pixels of the UI);
// - the key remap: with the screen upside down the front keys are at the top
//   in the other order, so LEFT and RIGHT are swapped (reading: as the book is
//   turned; elsewhere as the interface is). Touch-only boards have no front keys;
// - on a touch-only board whose touch panel did not answer, the held-key
//   fallback (Buttons::setHoldFallback).
namespace Input {
void install(AppContext& ctx);
ButtonId orient(ButtonId physical);
}
