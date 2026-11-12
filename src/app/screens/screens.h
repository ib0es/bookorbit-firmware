#pragma once

// Per-screen input handlers. Each handler reacts to one button event for its
// AppState and returns; it never blocks. Messages and confirmations go through
// Modal (ui/modal.h), long network operations through SyncService.

#include <Arduino.h>
#include "../app_state.h"
#include "../../platform/hal/buttons.h"

namespace HomeScreen {
void handleEvent(AppContext& ctx, const ButtonEvent& event);
}

namespace CatalogScreen {
void handleEvent(AppContext& ctx, const ButtonEvent& event);
}

namespace LibraryScreen {
void handleEvent(AppContext& ctx, const ButtonEvent& event);
// Opens the menu of the selected book (LEFT key, or a long press on the book).
void openBookMenu(AppContext& ctx);
// Lists the formats of the selected book (book menu "Format"); false if it has one.
bool openFormatsOfSelected(AppContext& ctx);
void handleBookMenuEvent(AppContext& ctx, const ButtonEvent& event);
}

namespace ReaderScreen {
void handleReadingEvent(AppContext& ctx, const ButtonEvent& event);
void handleTocEvent(AppContext& ctx, const ButtonEvent& event);
void handleMenuEvent(AppContext& ctx, const ButtonEvent& event);
void handleFontMenuEvent(AppContext& ctx, const ButtonEvent& event);
void handleTypefaceMenuEvent(AppContext& ctx, const ButtonEvent& event);
void handleAppearanceEvent(AppContext& ctx, const ButtonEvent& event);
void handleBookmarksEvent(AppContext& ctx, const ButtonEvent& event);
void handleStatusBarEvent(AppContext& ctx, const ButtonEvent& event);

// Opens a book, restores the position (xpath > progress > page), records it as
// lastBook and switches to STATE_READING. On failure shows a message and
// returns false (the current screen is redrawn when the message closes).
bool openBook(AppContext& ctx, const String& path, const String& title, const String& author,
              const String& coverPath, float progress, int page, const String& xpath,
              const String& docId, int fileId = 0);

// Wake-from-sleep fast path: reopens lastBook at its saved position.
bool resumeLastBook(AppContext& ctx);
}

namespace SettingsScreen {
void handleSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleWifiSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleServerSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleSyncSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleReadingSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleSleepCoverSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleLanguageSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleSdSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleUsbMscSettingsEvent(AppContext& ctx, const ButtonEvent& event);
void handleUsbMscEvent(AppContext& ctx, const ButtonEvent& event);
void handleUsbMscTap(AppContext& ctx, int action);
void handleFrontlightEvent(AppContext& ctx, const ButtonEvent& event);
void handleFrontlightTap(AppContext& ctx, int action, int value, int16_t x);
// The frontlight panel pulled down from the top edge (a swipe), a modal over the
// current screen. False where there is no light or on a busy screen.
bool openFrontlightFrom(AppContext& ctx);
// Touch on the frontlight screen (hit action/value from settings_view, tap x).
}
