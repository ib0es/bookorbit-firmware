#pragma once

#include <Arduino.h>

/**
 * Default configuration for Book Orbit X4 Client.
 * These can be overridden here or loaded dynamically from /config.json on the MicroSD card.
 */

// Credentials live in include/secrets.h (not committed; template:
// include/secrets.example.h). Without it the defaults are empty and the
// values come from /config.json on the SD card or from Settings.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef DEFAULT_WIFI_SSID
#define DEFAULT_WIFI_SSID ""
#endif
#ifndef DEFAULT_WIFI_PASSWORD
#define DEFAULT_WIFI_PASSWORD ""
#endif
#ifndef DEFAULT_BOOKORBIT_HOST
#define DEFAULT_BOOKORBIT_HOST ""
#endif
#ifndef DEFAULT_BOOKORBIT_USER
#define DEFAULT_BOOKORBIT_USER ""
#endif
#ifndef DEFAULT_BOOKORBIT_PASS
#define DEFAULT_BOOKORBIT_PASS ""
#endif
#ifndef DEFAULT_BOOKORBIT_TOKEN
#define DEFAULT_BOOKORBIT_TOKEN ""
#endif

// Book Orbit API endpoints
#define BOOKORBIT_OPDS_PATH        "/api/v1/opds"
#define BOOKORBIT_KOSYNC_PATH      "/api/v1/koreader"
#define BOOKORBIT_HEALTH_PATH      "/api/v1/health"

// OPDS catalog limits. The feed itself is parsed as a stream (one <entry> in RAM
// at a time); these bound the resulting in-memory book list on the 400 KB C3.
// How many books are kept is the memory tier's (src/platform/tiers/*.h).
// 1 = fast page refreshes return before the panel finishes (~0.3 s earlier), at the
// cost of a second 48 KB frame buffer kept on the heap for good (no PSRAM on the C3).
#ifndef EPD_ASYNC_FAST_REFRESH
#define EPD_ASYNC_FAST_REFRESH     0
#endif

#define OPDS_MAX_PAGES             20           // follow at most this many rel="next" pages
#define OPDS_MIN_FREE_HEAP         (6 * 1024)   // emergency stop only: with a book open the C3 can be at ~12-25KB during sync

// Device Settings (what differs between boards: src/platform/devices/*.h)
#define DEFAULT_LANGUAGE           "en"
#define FULL_REFRESH_EVERY_PAGES   5    // Trigger a full E-Ink refresh every N pages to eliminate ghosting
#ifndef SLEEP_TIMEOUT_MS
#define SLEEP_TIMEOUT_MS           (5 * 60 * 1000) // Deep sleep after 5 minutes of inactivity
#endif

// Display Controller Override (X4 only; X3 autodetects UC8253 vs UC8279d via Display.detectBoard()):
// 0 = AUTO (auto-detect via NVS and hardware BUSY probe)
// 1 = Force SSD1677/UC8179 primary controller on X4
#define DISPLAY_CONTROLLER_FORCE   1

// Display Rotation:
// 3 = Portrait upright (default, 480x800 on X4 / 528x792 on X3)
// 1 = Portrait inverted (180 deg upside-down)
// 0 = Landscape (800x480 on X4 / 792x528 on X3)
// 2 = Landscape inverted
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION           3
#endif
