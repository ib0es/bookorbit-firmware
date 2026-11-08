#pragma once

#include <Arduino.h>
#include "../../app/app_state.h"

void drawLibraryScreen(AppContext& ctx, bool fullRefresh = false);
// Rows per page of the last drawn library list (for swipe paging).
int libraryRowsPerPage();
