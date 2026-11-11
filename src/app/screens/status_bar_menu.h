#pragma once

// The status bar menu (Appearance > Status bar): what the lines above and
// below the text show. Rows step through their values; the reader gets them
// when the menus are left (with the rest of Appearance).

#include <Arduino.h>

#include "../app_state.h"
#include "../../reader/book_reader.h"

namespace StatusBarMenu {

enum Row : int { TOP, PAGES, PERCENT, BATTERY, BOTTOM, CLOCK, TIMEZONE, COUNT };

const char* label(int row);
const char* value(const AppConfig& cfg, int row, char* buf, size_t len);
void step(AppConfig& cfg, int row, int dir);
BookReader::StatusBar options(const AppConfig& cfg);

}  // namespace StatusBarMenu
