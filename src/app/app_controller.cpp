#include "app_controller.h"
#include "../hal/mem_stats.h"
#include <esp_sleep.h>
#include <PowerManager.h>
#include <BoardConfig.h>
#include "config.h"
#include "screens/screens.h"
#include "screens/appearance.h"
#include "orientation.h"
#include "screens/status_bar_menu.h"
#include "../services/bookmarks.h"
#include "touch_router.h"
#include "../hal/display.h"
#include "../hal/buttons.h"
#include "../hal/battery.h"
#include "../hal/rtc.h"
#include "../hal/imu.h"
#include "../hal/frontlight.h"
#include "../hal/storage.h"
#include "../services/sync_clock.h"
#include "../ui/font.h"
#include "../ui/modal.h"
#include "../ui/kit/ui_kit.h"
#include "../net/bookorbit_client.h"
#include "../reader/reader.h"
#include "../services/library_manager.h"
#include "../services/power_service.h"
#include "../services/sync_service.h"
#include "../services/progress_store.h"
#include "../ui/views/home_view.h"
#include "../ui/views/catalog_view.h"
#include "../ui/views/library_view.h"
#include "../ui/views/settings_view.h"
#include "../ui/views/reader_menu_view.h"
#include "../ui/views/book_menu_view.h"

namespace {
AppContext* g_ctxForKeys = nullptr;
bool keysUpsideDown() {
    if (!g_ctxForKeys || uikit::touchOnly()) return false;
    const int o = g_ctxForKeys->currentState == STATE_READING ? Reader.orientation() : g_ctxForKeys->config.uiOrientation;
    return (o & 3) == 2;
}
}  // namespace

void AppController::setup() {
    // 0. Disable any surviving deep-sleep pin holds from previous sleep cycle
    gpio_deep_sleep_hold_dis();

    // 1. Initialize Serial over USB CDC (GPIO 18/19)
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0); // Non-blocking: prevent freezes when USB is connected but host terminal is closed
#endif
    delay(20);
    Serial.println("\n=============================================");
    Serial.printf("   %s - Book Orbit Custom Client\n", uikit::deviceDisplayName());
    Serial.println("=============================================");

    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    bool isWakeFromSleep = (wakeupCause != ESP_SLEEP_WAKEUP_UNDEFINED);
    Serial.printf("[Power] Boot reason: %s\n", isWakeFromSleep ? "Deep Sleep Wakeup" : "Power On / Reset");

    // Accidental wake-up protection
    PowerService::handleAccidentalWakeup();

    MemStats::mark("setup start");   // what the core and its tasks hold before our code
    // Ensure Wi-Fi radio is completely shut down at boot for zero background lag
    WiFi.mode(WIFI_OFF);
    BookOrbit.disconnectWiFi();
    MemStats::mark("Wi-Fi off");

    // 2. Initialize HAL components
    // Board power rails first. Picco: GPIO4 latch and the active-low GPIO0 rail
    // that powers the shared I2C bus (touch 0x24, frontlight, RTC). Without it
    // the touch controller is unpowered when InputManager probes it, and touch
    // stays disabled for the whole session.
    BoardConfig::holdPowerRails();
    BoardConfig::releaseSdRail();
    Battery.begin();
    Buttons.begin();
    MemStats::mark("battery, buttons");
    // FreeInk's bring-up order for boards whose SD card shares the display SPI
    // bus (X4/X3): board/controller detection, then the SD card, then the
    // panel. Arduino-ESP32 maps the bus pins on the first SPI.begin() only:
    // SDCardManager maps SCLK/MISO/MOSI, the panel driver no MISO (the SSD1677
    // is write-only), so with the panel first the card never got MISO
    // ("MicroSD storage not available"). The detection bit-bangs the shared
    // pins, so it goes before the SD card takes the bus.
    Display.detectBoard();
    Storage.begin();
    MemStats::mark("SD card");
    BoardRtc.begin();
    SyncClock::initFromRtc();
    BoardImu.begin();
    _autoRotate.reset();
    _lastImuPollMs = 0;
    _lastClockCheckMs = 0;
    _lastClockMinute = -1;
    Display.begin();
    // A key pressed while the panel refreshes (a full refresh blocks ~3.8 s) is
    // caught then and answered right after, not lost.
    Display.setRefreshPoll([] { Buttons.update(); });
    MemStats::mark("display");
    uikit::begin();      // FreeInkUI target over the same framebuffer
    MemStats::mark("FreeInkUI");
    Font.begin(Display); // book fonts (src/ui/book_fonts.h)
    MemStats::mark("book fonts");

    // After a modal closes, the screen of the current state is redrawn.
    Modal.setRedrawHook([this](bool full) { redrawCurrentScreen(full); });

    // 3. Load configuration from SD card if present
    _ctx.config.load();
    Frontlight.begin();
    if (Buttons.hasLightSwitch()) Frontlight.setEnabled(Buttons.lightSwitchOn());
    g_ctxForKeys = &_ctx;
    applyConfig();
    Reader.setBookmarkProbe([](int spine, uint32_t from, uint32_t to) { return Bookmarks.findIn(spine, from, to) >= 0; });
    MemStats::mark("config, frontlight, reader");

    // 4. Per-book progress, scan local books on SD, 5. load last read book state
    ProgressStore::instance().load();
    MemStats::mark("progress.json");
    auto& lib = LibraryManager::getInstance();
    // Woken up in a book: the page comes first, the library scan (the card's
    // folders and catalog.json) when the book is left (LibraryManager::ensureScanned).
    // Restarted to open a book that did not fit in the cut-up heap: as after sleep.
    const bool restartedIntoBook = PowerService::takeRestartIntoBook();
    const bool resumeBook = restartedIntoBook || (isWakeFromSleep && PowerService::getSavedRtcState() == STATE_READING);
    if (!resumeBook) lib.scanLocalBooks();
    lib.initLastBook();
    MemStats::mark("library");

    // 6. Fast wake-up: if we went to sleep while reading, resume that book immediately.
    // After sleep (Settings > Synchronization > After sleep): the current book is
    // synced in the background while the reader / home screen is already up.
    auto syncAfterWake = [&]() {
        if (isWakeFromSleep && _ctx.config.syncOnWake && lib.lastBook.filePath.length() > 0) {
            lib.currentRecord();   // make sure the book has a record to sync
            SyncService::startBackgroundUpload(_ctx, lib.lastBook.fileId, lib.lastBook.filePath, /*onlyIfDirty=*/false);
        }
    };
    if (resumeBook && ReaderScreen::resumeLastBook(_ctx)) {
        MemStats::mark("book resumed");
        MemStats::printMarks();
        Buttons.clearEvents();
        _ctx.lastActivityTime = millis();
        syncAfterWake();
        return;
    }

    lib.ensureScanned();   // the book could not be resumed

    // 7. Cold boot progress sync only if user explicitly turned on syncOnBoot in Settings
    if (!isWakeFromSleep) SyncService::syncOnBootProgress(_ctx);

    // 8. Open Main / Home screen with last book cover
    MemStats::mark("boot done");
    MemStats::printMarks();
    MemStats::log("boot done");
    MemStats::logMap("boot done");
    _ctx.currentState = STATE_HOME;
    drawHomeScreen(_ctx, true); // Full refresh on boot
    // Leftovers of deleted books (cold boot only: keeps wake fast), once the screen is up.
    if (!isWakeFromSleep) lib.cleanOrphanFiles();
    syncAfterWake();
    Buttons.clearEvents();
    _ctx.lastActivityTime = millis();
}

void AppController::applyConfig() {
    const AppConfig& c = _ctx.config;
    I18n::setLanguage(c.language);
    Frontlight.apply(c.frontlight, c.frontlightWarm);
    BoardImu.setActive(c.autoRotate);
    _autoRotate.reset();
    Reader.setFullRefreshEvery(c.fullRefreshEvery);
    Reader.setSharpImages(c.sharpImages);
    Reader.setFontFamily(c.readerFont);
    Reader.setLayoutOptions(Appearance::layoutOptions(c));
    applyOrientation(c.uiOrientation, c.orientation);
    Reader.setStatusBar(StatusBarMenu::options(c));
    Reader.setFontSize(c.readerFontSize);
    LibraryManager::getInstance().setHideCompleted(c.hideCompleted);
}

// The background layout waits this long after a key: quick page turns run
// without it. A slice (one engine step) blocks the keys for 0.1-0.35 s on the
// X4, so after each one the loop rests as long as the slice took: a key
// pressed meanwhile is answered at once, and one pressed during a slice
// waits at most that slice.
static constexpr uint32_t kPrelayoutIdleMs = 2500;
static uint32_t s_restUntil = 0;

void AppController::loop() {
    Buttons.update();
    Battery.tick();
    SyncService::tickBackground(_ctx);

    if (Buttons.takeLightSwitchChange()) {
        _ctx.lastActivityTime = millis();
        const bool on = Buttons.lightSwitchOn();
        // Switched on with the light set to 0: pick a usable level, else the
        // switch would seem to do nothing.
        if (on && _ctx.config.frontlight == 0) {
            _ctx.config.frontlight = 50;
            Frontlight.apply(_ctx.config.frontlight, _ctx.config.frontlightWarm);
            _ctx.config.save();
        }
        Frontlight.setEnabled(on);
    }

    ButtonEvent event;
    if (Buttons.getEvent(event)) {
        const uint32_t tEvent = millis();
        _ctx.lastActivityTime = tEvent;
        Serial.printf("[Event] Button: %s | Action: %d at %lu ms\n", Buttons.getName(event.id), event.action,
                      (unsigned long)tEvent);

        if (event.id == BTN_POWER) {
            // Power key: manual sleep (also while a dialog is open)
            PowerService::enterDeepSleep(_ctx);
            return;
        }
        if (!Modal.handleEvent(event)) {
            if (event.id == BTN_TAP) {
                ButtonEvent translated;
                if (TouchRouter::translate(_ctx, event, translated)) dispatch(translated);
            } else if (event.id >= BTN_SWIPE_LEFT && event.id <= BTN_SWIPE_DOWN) {
                ButtonEvent translated;
                if (TouchRouter::translateSwipe(_ctx, event, translated)) dispatch(translated);
            } else if (keysUpsideDown() && (event.id == BTN_LEFT || event.id == BTN_RIGHT)) {
                // The screen upside down: the front keys are at the top, in the other order
                // (reading: as the book is turned; elsewhere as the interface is).
                ButtonEvent turned = event;
                turned.id = event.id == BTN_LEFT ? BTN_RIGHT : BTN_LEFT;
                dispatch(turned);
            } else {
                dispatch(event);
            }
        }
        // From the key to the screen updated (drawing and the e-ink refresh included).
        Serial.printf("[Event] Handled in %lu ms\n", (unsigned long)(millis() - tEvent));
        // The idle timer starts when the work is done: a sync or a download that
        // took longer than the sleep timeout must not send the device to sleep
        // the moment it ends.
        _ctx.lastActivityTime = millis();
    }

    Modal.tick();

    // Auto-rotate from IMU when enabled (paused while modals or sync dialogs are open).
    if (_ctx.config.autoRotate && BoardImu.active() && !Modal.isActive() && !SyncService::backgroundActive()) {
        const uint32_t nowMs = millis();
        if (nowMs - _lastImuPollMs >= 250) {
            _lastImuPollMs = nowMs;
            float ax = 0.0f, ay = 0.0f, az = 0.0f;
            if (BoardImu.readAccel(ax, ay, az)) {
                const ImuCalibration cal{_ctx.config.imuRot, _ctx.config.imuFlipX, _ctx.config.imuFlipY};
                const int nextOrient = _autoRotate.update(ax, ay, az, _ctx.config.uiOrientation, nowMs, cal);
                if (nextOrient >= 0 && nextOrient != _ctx.config.uiOrientation) {
                    Serial.printf("[App] Auto-rotated UI %d -> %d (ax=%.2f ay=%.2f az=%.2f)\n",
                                  _ctx.config.uiOrientation, nextOrient, ax, ay, az);
                    _ctx.config.uiOrientation = nextOrient;
                    applyOrientation(_ctx.config.uiOrientation, _ctx.config.orientation);
                    _ctx.lastActivityTime = nowMs;
                    redrawCurrentScreen(true);
                }
            }
        }
    } else {
        _autoRotate.reset();
    }

    // Refresh reader status bar on minute boundaries when the clock is enabled.
    if (_ctx.currentState == STATE_READING && _ctx.config.sbClock && !Modal.isActive() &&
        !SyncService::backgroundActive()) {
        const uint32_t nowMs = millis();
        if (nowMs - _lastClockCheckMs >= 1000) {
            _lastClockCheckMs = nowMs;
            const int32_t curMin = SyncClock::currentLocalMinute(_ctx.config.utcOffsetMin);
            if (_lastClockMinute < 0) {
                _lastClockMinute = curMin;
            } else if (curMin >= 0 && curMin != _lastClockMinute) {
                _lastClockMinute = curMin;
                redrawCurrentScreen(false);
            }
        }
    } else {
        _lastClockMinute = -1;
    }

    // Auto deep sleep on inactivity (only when on battery, never while plugged into USB)
    PowerService::sleepIfIdle(_ctx);
    // An almost flat battery: save and sleep before the board browns out.
    PowerService::checkBattery(_ctx);

    // While a page is being read, the chapters ahead are laid out in slices of a
    // few tens of milliseconds (buttons are polled between them). Leaving the page
    // or a radio coming up stops it: both need its memory.
    // The reader's own menus keep it (paused): the chapter on screen may still be
    // coming from it, and dropping it would mean laying the chapter out again.
    const AppState st = _ctx.currentState;
    const bool readerScreens = st == STATE_READING || st == STATE_TOC || st == STATE_READER_MENU || st == STATE_FONT_MENU ||
                               st == STATE_TYPEFACE_MENU || st == STATE_APPEARANCE_MENU || st == STATE_BOOKMARKS ||
                               st == STATE_STATUSBAR_MENU;
    if (!readerScreens || SyncService::backgroundActive()) {
        if (Reader.backgroundActive()) Reader.stopBackground();
    } else if (st == STATE_READING && !Modal.isActive() && millis() - _ctx.lastActivityTime > kPrelayoutIdleMs &&
               (int32_t)(millis() - s_restUntil) >= 0) {
        // Page turns not on the card yet go first: a reset or a flat battery must not lose them.
        if (LibraryManager::getInstance().persistIfDue()) return;
        const uint32_t t0 = millis();
        const bool worked = Reader.tickBackground();
        const uint32_t took = millis() - t0;
        if (took > 400) Serial.printf("[Reader] Background slice took %lu ms\n", (unsigned long)took);
        s_restUntil = millis() + took;
        if (worked) return;
    }

    delay(10);
}

void AppController::dispatch(const ButtonEvent& event) {
    switch (_ctx.currentState) {
        case STATE_HOME:              HomeScreen::handleEvent(_ctx, event); break;
        case STATE_CATALOG:           CatalogScreen::handleEvent(_ctx, event); break;
        case STATE_LIBRARY:           LibraryScreen::handleEvent(_ctx, event); break;
        case STATE_READING:           ReaderScreen::handleReadingEvent(_ctx, event); break;
        case STATE_TOC:               ReaderScreen::handleTocEvent(_ctx, event); break;
        case STATE_READER_MENU:       ReaderScreen::handleMenuEvent(_ctx, event); break;
        case STATE_SETTINGS:          SettingsScreen::handleSettingsEvent(_ctx, event); break;
        case STATE_WIFI_SETTINGS:     SettingsScreen::handleWifiSettingsEvent(_ctx, event); break;
        case STATE_SERVER_SETTINGS:   SettingsScreen::handleServerSettingsEvent(_ctx, event); break;
        case STATE_LANGUAGE_SETTINGS: SettingsScreen::handleLanguageSettingsEvent(_ctx, event); break;
        case STATE_FRONTLIGHT_SETTINGS: SettingsScreen::handleFrontlightEvent(_ctx, event); break;
        case STATE_BOOK_MENU:         LibraryScreen::handleBookMenuEvent(_ctx, event); break;
        case STATE_FONT_MENU:         ReaderScreen::handleFontMenuEvent(_ctx, event); break;
        case STATE_TYPEFACE_MENU:     ReaderScreen::handleTypefaceMenuEvent(_ctx, event); break;
        case STATE_APPEARANCE_MENU:   ReaderScreen::handleAppearanceEvent(_ctx, event); break;
        case STATE_BOOKMARKS:         ReaderScreen::handleBookmarksEvent(_ctx, event); break;
        case STATE_STATUSBAR_MENU:    ReaderScreen::handleStatusBarEvent(_ctx, event); break;
        case STATE_SYNC_SETTINGS:     SettingsScreen::handleSyncSettingsEvent(_ctx, event); break;
        case STATE_SLEEP_COVER_SETTINGS: SettingsScreen::handleSleepCoverSettingsEvent(_ctx, event); break;
        case STATE_SD_SETTINGS:       SettingsScreen::handleSdSettingsEvent(_ctx, event); break;
        case STATE_SYNCING:           break;
    }
}

void AppController::redrawCurrentScreen(bool fullRefresh) {
    switch (_ctx.currentState) {
        case STATE_HOME:              drawHomeScreen(_ctx, fullRefresh); break;
        case STATE_CATALOG:           drawCatalogScreen(_ctx, fullRefresh); break;
        case STATE_LIBRARY:           drawLibraryScreen(_ctx, fullRefresh); break;
        case STATE_READING:           Reader.renderCurrentPage(); break;
        case STATE_TOC:               drawTocScreen(_ctx, fullRefresh); break;
        case STATE_READER_MENU:       drawReaderMenuScreen(_ctx, fullRefresh); break;
        case STATE_SETTINGS:          drawSettingsScreen(_ctx, fullRefresh); break;
        case STATE_WIFI_SETTINGS:     drawWifiSettingsScreen(_ctx, fullRefresh); break;
        case STATE_SERVER_SETTINGS:   drawServerSettingsScreen(_ctx, fullRefresh); break;
        case STATE_LANGUAGE_SETTINGS: drawLanguageSettingsScreen(_ctx, fullRefresh); break;
        case STATE_FRONTLIGHT_SETTINGS: drawFrontlightScreen(_ctx, fullRefresh); break;
        case STATE_BOOK_MENU:         drawBookMenuScreen(_ctx, fullRefresh); break;
        case STATE_FONT_MENU:         drawFontMenuScreen(_ctx, fullRefresh); break;
        case STATE_TYPEFACE_MENU:     drawTypefaceMenuScreen(_ctx, fullRefresh); break;
        case STATE_APPEARANCE_MENU:   drawAppearanceScreen(_ctx, fullRefresh); break;
        case STATE_BOOKMARKS:         drawBookmarksScreen(_ctx, fullRefresh); break;
        case STATE_STATUSBAR_MENU:    drawStatusBarScreen(_ctx, fullRefresh); break;
        case STATE_SYNC_SETTINGS:     drawSyncSettingsScreen(_ctx, fullRefresh); break;
        case STATE_SLEEP_COVER_SETTINGS: drawSleepCoverSettingsScreen(_ctx, fullRefresh); break;
        case STATE_SD_SETTINGS:       drawSdSettingsScreen(_ctx, fullRefresh); break;
        case STATE_SYNCING:           break;
    }
}
