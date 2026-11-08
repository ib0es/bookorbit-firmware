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
    STATE_SYNC_SETTINGS
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
    int sleepCoverFitMode = 1; // 0 = Scale / Fit (CrossPoint FIT), 1 = Crop / Fill (CrossPoint CROP)
    int readerFontSize = 2;   // book font size index: 0 = 12 pt ... 18 = 48 pt (FontEngine)
    int frontlight = 0;        // 0..100 %, 0 = off (boards with a frontlight)
    int frontlightWarm = 50;   // 0 = cool .. 100 = warm
    AppLanguage language = AppLanguage::EN;
    bool hideCompleted = false;

    bool load();
    bool save();
};

const int NUM_HOME_ITEMS = 4;      // 0=Recent, 1=Catalog, 2=Settings, 3=Sync
const int NUM_CATALOG_ITEMS = 9;   // Search, All, Libraries, Authors, Series, Folders, Downloaded, Completed, Toggle Hide Completed
const int NUM_SETTINGS_ITEMS = 8;  // + 1 (frontlight) on boards that have one, see settingsItemCount()
int settingsItemCount();
const int NUM_WIFI_SETTINGS_ITEMS = 6;
const int NUM_SERVER_SETTINGS_ITEMS = 4;
const int NUM_SYNC_SETTINGS_ITEMS = 4;
const int NUM_READER_MENU_ITEMS = 5;

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
    int selectedSyncSettingIndex = 0;     // font size submenu of the reader menu
    uint32_t lastActivityTime = 0;
};
