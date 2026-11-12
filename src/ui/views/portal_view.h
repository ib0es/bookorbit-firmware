#pragma once

#include <cstdint>

#include "../../core/ui_port.h"

void drawPortalScreen(const char* apSsid, const char* apPass, const char* url, int page = 0, bool fullRefresh = false);
PortalAction handlePortalEvent(int btnId, int btnAction, int16_t x, int16_t y, int& page);
