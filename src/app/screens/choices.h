#pragma once

// Settings whose value is one of a few: a list of their own (a choice
// screen) with the one in use checked, instead of stepping through them in
// place. Which setting the screen is about: AppContext::choiceKind.

#include <Arduino.h>

#include "../app_state.h"

namespace Choices {

enum Kind : int {
    FULL_REFRESH,   // Settings -> Reading: a full refresh every N pages, or never
    COUNT
};

const char* title(int kind);
int count(int kind);
const char* label(int kind, int index, char* buf, size_t len);
int active(const AppConfig& cfg, int kind);   // the index in use
void apply(AppConfig& cfg, int kind, int index);
AppState parent(int kind);                    // the screen the list was opened from

}  // namespace Choices

// Settings -> Screen orientation: the choices (Auto first on boards with a
// motion sensor, then the four turns) and the one in use.
int orientationChoiceCount();
int orientationChoiceActive(const AppConfig& cfg);
