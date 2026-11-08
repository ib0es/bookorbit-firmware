#include "app_controller.h"
#include "../hal/mem_stats.h"
#include <esp_sleep.h>
#include <PowerManager.h>
#include <BoardConfig.h>
#include "config.h"
#include "screens/screens.h"
#include "touch_router.h"
#include "../hal/display.h"
#include "../hal/buttons.h"
#include "../hal/battery.h"
#include "../hal/frontlight.h"
#include "../hal/storage.h"
#include "../ui/font.h"
#include "../ui/modal.h"
#include "../ui/kit/ui_kit.h"
#include "../net/bookorbit_client.h"
#include "../reader/simple_reader.h"
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

    // Ensure Wi-Fi radio is completely shut down at boot for zero background lag
    WiFi.mode(WIFI_OFF);
    BookOrbit.disconnectWiFi();

    // 2. Initialize HAL components
    // Board power rails first. Picco: GPIO4 latch and the active-low GPIO0 rail
    // that powers the shared I2C bus (touch 0x24, frontlight, RTC). Without it
    // the touch controller is unpowered when InputManager probes it, and touch
    // stays disabled for the whole session.
    BoardConfig::holdPowerRails();
    Battery.begin();
    Buttons.begin();
    // FreeInk's bring-up order for boards whose SD card shares the display SPI
    // bus (X4/X3): board/controller detection, then the SD card, then the
    // panel. Arduino-ESP32 maps the bus pins on the first SPI.begin() only:
    // SDCardManager maps SCLK/MISO/MOSI, the panel driver no MISO (the SSD1677
    // is write-only), so with the panel first the card never got MISO
    // ("MicroSD storage not available"). The detection bit-bangs the shared
    // pins, so it goes before the SD card takes the bus.
    Display.detectBoard();
    Storage.begin();
    Display.begin();
    uikit::begin();      // FreeInkUI target over the same framebuffer
    Font.begin(Display); // book fonts (src/ui/book_fonts.h)

    // After a modal closes, the screen of the current state is redrawn.
    Modal.setRedrawHook([this](bool full) { redrawCurrentScreen(full); });

    // 3. Load configuration from SD card if present
    _ctx.config.load();
    I18n::setLanguage(_ctx.config.language);
    Frontlight.begin();
    if (Buttons.hasLightSwitch()) Frontlight.setEnabled(Buttons.lightSwitchOn());
    Frontlight.apply(_ctx.config.frontlight, _ctx.config.frontlightWarm);
    Reader.setFontSize(_ctx.config.readerFontSize);

    // 4. Per-book progress, scan local books on SD, 5. load last read book state
    ProgressStore::instance().load();
    auto& lib = LibraryManager::getInstance();
    lib.setHideCompleted(_ctx.config.hideCompleted);
    lib.scanLocalBooks();
    lib.initLastBook();

    // 6. Fast wake-up: if we went to sleep while reading, resume that book immediately.
    // After sleep (Settings > Synchronization > After sleep): the current book is
    // synced in the background while the reader / home screen is already up.
    auto syncAfterWake = [&]() {
        if (isWakeFromSleep && _ctx.config.syncOnWake && lib.lastBook.filePath.length() > 0) {
            lib.currentRecord();   // make sure the book has a record to sync
            SyncService::startBackgroundUpload(_ctx, lib.lastBook.fileId, lib.lastBook.filePath, /*onlyIfDirty=*/false);
        }
    };
    if (isWakeFromSleep && PowerService::getSavedRtcState() == STATE_READING && ReaderScreen::resumeLastBook(_ctx)) {
        Buttons.clearEvents();
        _ctx.lastActivityTime = millis();
        syncAfterWake();
        return;
    }

    // 7. Cold boot progress sync only if user explicitly turned on syncOnBoot in Settings
    if (!isWakeFromSleep) {
        lib.cleanOrphanFiles();  // leftovers of deleted books (cold boot only: keeps wake fast)
        SyncService::syncOnBootProgress(_ctx);
    }

    // 8. Open Main / Home screen with last book cover
    MemStats::log("boot done");
    _ctx.currentState = STATE_HOME;
    drawHomeScreen(_ctx, true); // Full refresh on boot
    syncAfterWake();
    Buttons.clearEvents();
    _ctx.lastActivityTime = millis();
}

void AppController::loop() {
    Buttons.update();
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
        _ctx.lastActivityTime = millis();
        Serial.printf("[Event] Button: %s | Action: %d\n", Buttons.getName(event.id), event.action);

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
            } else {
                dispatch(event);
            }
        }
    }

    Modal.tick();

    // Auto deep sleep on inactivity (only when on battery, never while plugged into USB)
    PowerService::sleepIfIdle(_ctx);

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
        case STATE_SYNC_SETTINGS:     SettingsScreen::handleSyncSettingsEvent(_ctx, event); break;
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
        case STATE_SYNC_SETTINGS:     drawSyncSettingsScreen(_ctx, fullRefresh); break;
        case STATE_SYNCING:           break;
    }
}
