#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

namespace HomeHit {
enum { OpenBook = 1, Tile, PreviousBook, NextBook };
}

void drawHomeScreen(AppContext& ctx, bool fullRefresh = false);
