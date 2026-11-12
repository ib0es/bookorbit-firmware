#pragma once

#include <Arduino.h>
#include "config.h"
#include "../ui/i18n.h"
#include "../platform/device.h"

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
    STATE_STATUSBAR_MENU,
    STATE_SD_SETTINGS,
    STATE_USB_MSC_SETTINGS,
    STATE_USB_MSC,
    STATE_READING_SETTINGS,
    STATE_WEB_SETTINGS
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
    bool upDownTurnPages = false;   // reading: UP/DOWN turn pages (else they jump chapters)
    // Orientations, a quarter turn a step: 0 portrait, 1 landscape, 2 portrait upside
    // down, 3 landscape upside down. The interface (Settings) and the book on top
    // of it (Appearance): the pages are turned by both.
    int uiOrientation = 0;
    int orientation = 0;
    // IMU auto-rotate: on by default where the device profile says so (X3, X4 Classic).
    bool autoRotate = Platform::kDevice.autoRotateByDefault;
    int imuRot = 0;              // 0..3 quarter-turns for hardware IMU axis alignment
    bool imuFlipX = false;
    bool imuFlipY = false;
    // Status bar: top left 0 book title, 1 chapter, 2 none; page number 0 in the
    // book, 1 in the chapter, 2 none; bottom 0 chapter and progress bar, 1 bar, 2 none.
    int sbTop = 0, sbPages = 0, sbBottom = 0;
    bool sbPercent = false, sbBattery = true;
    bool sbClock = false;
    int utcOffsetMin = 0;        // -720..+840 (-12:00..+14:00), shown when sbClock is on
    int fullRefreshEvery = 5;    // pages, 0 = never
    int frontlight = 0;        // 0..100 %, 0 = off (boards with a frontlight)
    int frontlightWarm = 50;   // 0 = cool .. 100 = warm
    AppLanguage language = AppLanguage::EN;
    bool hideCompleted = false;
    bool usbMscEnabled = true;
    bool touchWakeEnabled = true;

    bool load();
    bool save();
};

const int NUM_HOME_ITEMS = 4;      // 0=Recent, 1=Catalog, 2=Settings, 3=Sync
const int NUM_CATALOG_ITEMS = 9;   // Search, All, Libraries, Authors, Series, Folders, Downloaded, Completed, Toggle Hide Completed
const int NUM_SETTINGS_ITEMS = 8;  // base count before optional Frontlight, USB-MSC, Touch Wake
bool hasUsbMscSetting();
bool hasTouchWakeSetting();
int settingsItemCount();
// The page orientation row keeps indices 0..8 identical across boards; board
// capabilities (USB-MSC, Touch Wake) follow after it.
int orientationSettingIndex();
int usbMscSettingIndex();
int touchWakeSettingIndex();
int webSettingIndex();
int readingSettingIndex();   // Settings -> Reading: just before screen orientation
const int NUM_WIFI_SETTINGS_ITEMS = 5;
const int NUM_WEB_SETTINGS_ITEMS = 1;
const int NUM_SERVER_SETTINGS_ITEMS = 4;
const int NUM_SYNC_SETTINGS_ITEMS = 4;
const int NUM_SLEEP_COVER_ITEMS = 3;
const int NUM_SD_SETTINGS_ITEMS = 2;   // reload / save /config.json
const int NUM_USB_MSC_ITEMS = 2;       // connect / enable-disable toggle
const int NUM_READER_MENU_ITEMS = 6;      // the light: a swipe down from the top edge, on any screen
int readerMenuItemCount();

struct AppContext {
    AppState currentState = STATE_HOME;
    AppConfig config;

    int selectedHomeIndex = 0;
    int selectedCatalogIndex = 0;
    int selectedSettingIndex = 0;
    int selectedWifiSettingIndex = 0;
    int selectedWebSettingIndex = 0;
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
    int selectedReadingSettingIndex = 0;
    int selectedBookmarkIndex = 0;   // 0 = add/remove this page's, then the bookmarks
    int selectedSyncSettingIndex = 0;
    int selectedSleepCoverIndex = 0;
    int selectedSdSettingIndex = 0;
    int selectedUsbMscIndex = 0;
    uint32_t lastActivityTime = 0;
};
