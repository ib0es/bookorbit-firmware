#pragma once

#include <Arduino.h>
#include "config.h"
#include "../ui/i18n.h"

enum AppState {
    STATE_HOME,
    STATE_CATALOG,
    STATE_LIBRARY,
    STATE_READING,
    STATE_SYNCING,
    STATE_SETTINGS,
    STATE_WIFI_SETTINGS,
    STATE_SERVER_SETTINGS,
    STATE_LANGUAGE_SETTINGS,
    STATE_TOC,
    STATE_READER_MENU,
    STATE_FRONTLIGHT_SETTINGS,
    STATE_BOOK_MENU,
    STATE_FONT_MENU,
    STATE_SYNC_SETTINGS,
    STATE_SLEEP_COVER_SETTINGS,
    STATE_TYPEFACE_MENU,
    STATE_APPEARANCE_MENU,
    STATE_BOOKMARKS,
    STATE_STATUSBAR_MENU
};

struct AppConfig {
    String wifiSsid = DEFAULT_WIFI_SSID;
    String wifiPass = DEFAULT_WIFI_PASSWORD;
    String serverHost = DEFAULT_BOOKORBIT_HOST;
    String bookOrbitUser = DEFAULT_BOOKORBIT_USER;
    String bookOrbitPass = DEFAULT_BOOKORBIT_PASS;
    String authToken = DEFAULT_BOOKORBIT_TOKEN;
    bool syncOnBoot = false;
    bool syncOnSleep = false;
    bool syncOnClose = true;   // upload the position in the background after closing a book
    bool syncOnWake = false;   // sync the current book in the background after waking up
    bool coverOnSleep = true;
    int sleepCoverFitMode = 0; // 0 = Scale / Fit (Keep aspect ratio), 1 = Crop / Fill (Fullscreen crop)
    int readerFontSize = 2;   // book font size index: 0 = 12 pt ... 18 = 48 pt (FontEngine)
    String readerFont;        // book font family: "" = the built-in one, else one installed from /fonts
    // Appearance (BookReader::LayoutOptions and the refresh rate).
    int lineSpacing = 100;       // %
    int paragraphSpacing = 100;  // %
    int marginPx = 20;
    bool justify = true;
    bool hyphenation = true;
    bool keepLines = true;
    bool bookStyles = true;
    bool focusReading = false;
    bool sharpImages = false;    // pictures: threshold instead of dithering
    // Orientations, a quarter turn a step: 0 portrait, 1 landscape, 2 portrait upside
    // down, 3 landscape upside down. The interface (Settings) and the book on top
    // of it (Appearance): the pages are turned by both.
    int uiOrientation = 0;
    int orientation = 0;
    // Status bar: top left 0 book title, 1 chapter, 2 none; page number 0 in the
    // book, 1 in the chapter, 2 none; bottom 0 chapter and progress bar, 1 bar, 2 none.
    int sbTop = 0, sbPages = 0, sbBottom = 0;
    bool sbPercent = false, sbBattery = true;
    int fullRefreshEvery = 5;    // pages, 0 = never
    int frontlight = 0;        // 0..100 %, 0 = off (boards with a frontlight)
    int frontlightWarm = 50;   // 0 = cool .. 100 = warm
    AppLanguage language = AppLanguage::EN;
    bool hideCompleted = false;

    bool load();
    bool save();
};

const int NUM_HOME_ITEMS = 4;      // 0=Recent, 1=Catalog, 2=Settings, 3=Sync
const int NUM_CATALOG_ITEMS = 9;   // Search, All, Libraries, Authors, Series, Folders, Downloaded, Completed, Toggle Hide Completed
const int NUM_SETTINGS_ITEMS = 9;  // + 1 (frontlight) on boards that have one, see settingsItemCount()
int settingsItemCount();
// The page orientation row: the last one (after the frontlight where there is one).
inline int orientationSettingIndex() { return settingsItemCount() - 1; }
const int NUM_WIFI_SETTINGS_ITEMS = 6;
const int NUM_SERVER_SETTINGS_ITEMS = 4;
const int NUM_SYNC_SETTINGS_ITEMS = 4;
const int NUM_SLEEP_COVER_ITEMS = 3;
const int NUM_READER_MENU_ITEMS = 6;

struct AppContext {
    AppState currentState = STATE_HOME;
    AppConfig config;

    int selectedHomeIndex = 0;
    int selectedCatalogIndex = 0;
    int selectedSettingIndex = 0;
    int selectedWifiSettingIndex = 0;
    int selectedServerSettingIndex = 0;
    int selectedLanguageIndex = 0;
    int selectedTocIndex = 0;
    int selectedReaderMenuIndex = 0;
    int selectedFrontlightIndex = 0;
    int selectedBookMenuIndex = 0;
    int selectedFontIndex = 0;
    int selectedTypefaceIndex = 0;
    int selectedAppearanceIndex = 0;
    int selectedStatusBarIndex = 0;
    int selectedBookmarkIndex = 0;   // 0 = add/remove this page's, then the bookmarks
    int selectedSyncSettingIndex = 0;
    int selectedSleepCoverIndex = 0;
    uint32_t lastActivityTime = 0;
};
