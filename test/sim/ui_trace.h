#pragma once
#include "ui/kit/ui_kit.h"

// Linked only into status_bar_tests; firmware and normal simulator objects omit it.
namespace UiTrace {
void record(const char* kind, uikit::Rect rect, const char* text = "", uint8_t maxLines = 1,
            freeink::ui::FontId font = uikit::FontSmall);
}
