#include "screen_table.h"

#include "screens/screens.h"
#include "../reader/reader.h"
#include "../ui/views/book_menu_view.h"
#include "../ui/views/catalog_view.h"
#include "../ui/views/home_view.h"
#include "../ui/views/library_view.h"
#include "../ui/views/reader_menu_view.h"
#include "../ui/views/settings_view.h"

namespace {

void drawReading(AppContext&, bool) { Reader.renderCurrentPage(); }
void drawUsbMsc(AppContext& ctx, bool fullRefresh) { drawUsbMscScreen(ctx, fullRefresh); }

using C = AppContext;
const ScreenEntry kScreens[] = {
    // state                      input                                        drawing                       tap selects                     paged
    { STATE_HOME,                 HomeScreen::handleEvent,                     drawHomeScreen,               nullptr,                        false },
    { STATE_CATALOG,              CatalogScreen::handleEvent,                  drawCatalogScreen,            &C::selectedCatalogIndex,       true  },
    { STATE_LIBRARY,              LibraryScreen::handleEvent,                  drawLibraryScreen,            nullptr,                        false },
    { STATE_READING,              ReaderScreen::handleReadingEvent,            drawReading,                  nullptr,                        false },
    { STATE_SYNCING,              nullptr,                                     nullptr,                      nullptr,                        false },
    { STATE_SETTINGS,             SettingsScreen::handleSettingsEvent,         drawSettingsScreen,           &C::selectedSettingIndex,       true  },
    { STATE_WIFI_SETTINGS,        SettingsScreen::handleWifiSettingsEvent,     drawWifiSettingsScreen,       &C::selectedWifiSettingIndex,   true  },
    { STATE_SERVER_SETTINGS,      SettingsScreen::handleServerSettingsEvent,   drawServerSettingsScreen,     &C::selectedServerSettingIndex, true  },
    { STATE_LANGUAGE_SETTINGS,    SettingsScreen::handleLanguageSettingsEvent, drawLanguageSettingsScreen,   &C::selectedLanguageIndex,      true  },
    { STATE_TOC,                  ReaderScreen::handleTocEvent,                drawTocScreen,                &C::selectedTocIndex,           false },
    { STATE_READER_MENU,          ReaderScreen::handleMenuEvent,               drawReaderMenuScreen,         &C::selectedReaderMenuIndex,    true  },
    { STATE_FRONTLIGHT_SETTINGS,  SettingsScreen::handleFrontlightEvent,       drawFrontlightScreen,         nullptr,                        false },
    { STATE_BOOK_MENU,            LibraryScreen::handleBookMenuEvent,          drawBookMenuScreen,           &C::selectedBookMenuIndex,      true  },
    { STATE_FONT_MENU,            ReaderScreen::handleFontMenuEvent,           drawFontMenuScreen,           &C::selectedFontIndex,          false },
    { STATE_SYNC_SETTINGS,        SettingsScreen::handleSyncSettingsEvent,     drawSyncSettingsScreen,       &C::selectedSyncSettingIndex,   true  },
    { STATE_SLEEP_COVER_SETTINGS, SettingsScreen::handleSleepCoverSettingsEvent, drawSleepCoverSettingsScreen, &C::selectedSleepCoverIndex, true  },
    { STATE_TYPEFACE_MENU,        ReaderScreen::handleTypefaceMenuEvent,       drawTypefaceMenuScreen,       &C::selectedTypefaceIndex,      true  },
    { STATE_APPEARANCE_MENU,      ReaderScreen::handleAppearanceEvent,         drawAppearanceScreen,         &C::selectedAppearanceIndex,    true  },
    { STATE_BOOKMARKS,            ReaderScreen::handleBookmarksEvent,          drawBookmarksScreen,          &C::selectedBookmarkIndex,      true  },
    { STATE_STATUSBAR_MENU,       ReaderScreen::handleStatusBarEvent,          drawStatusBarScreen,          &C::selectedStatusBarIndex,     false },
    { STATE_SD_SETTINGS,          SettingsScreen::handleSdSettingsEvent,       drawSdSettingsScreen,         &C::selectedSdSettingIndex,     true  },
    { STATE_USB_MSC_SETTINGS,     SettingsScreen::handleUsbMscSettingsEvent,   drawUsbMscSettingsScreen,     &C::selectedUsbMscIndex,        true  },
    { STATE_USB_MSC,              SettingsScreen::handleUsbMscEvent,           drawUsbMsc,                   nullptr,                        false },
    { STATE_READING_SETTINGS,     SettingsScreen::handleReadingSettingsEvent,  drawReadingSettingsScreen,    &C::selectedReadingSettingIndex, false },
};

}  // namespace

namespace Screens {

const ScreenEntry* find(AppState state) {
    for (const ScreenEntry& s : kScreens) {
        if (s.state == state) return &s;
    }
    return nullptr;
}

}  // namespace Screens
