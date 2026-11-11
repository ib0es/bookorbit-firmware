#include "power_service.h"
#include "../ui/kit/ui_kit.h"
#include <cstdlib>
#include <esp_sleep.h>
#include <WiFi.h>
#include <PowerManager.h>
#include <BoardConfig.h>
#include "../platform/hal/display.h"
#include "../platform/hal/battery.h"
#include "../platform/hal/imu.h"
#include "../platform/hal/frontlight.h"
#include "../platform/hal/storage.h"
#include "../platform/hal/usb_msc.h"
#include "../ui/font.h"
#include "../ui/jpeg_viewer.h"
#include "../ui/modal.h"
#include "../ui/views/status_modal.h"
#include "../ui/i18n.h"
#include "library_manager.h"
#include "../core/light_sleep_policy.h"
#include "../core/power_policy.h"
#include "sync_service.h"
#include "../reader/reader.h"
#include "../net/bookorbit_client.h"
#include "../app/app_controller.h"
#include "../platform/hal/buttons.h"
#include "../ui/text_utils.h"
#include "../platform/device.h"
#include "../platform/system.h"

namespace {
uint32_t s_lastBatteryCheck = 0;

uint32_t batteryCheckIntervalMs() { return Platform::batteryCheckIntervalMs(); }
uint32_t sleepTimeoutMs() { return Platform::sleepTimeoutMs(); }
bool isUsbCdcConnected() { return Platform::usbSerialConnected(); }
}  // namespace

RTC_DATA_ATTR static int rtcSavedState = STATE_HOME;
// Kept through a software restart: RTC_DATA_ATTR variables are loaded again by the
// bootloader at every boot except a deep-sleep wake-up (the first version lost the
// flag and the restart ended on the home screen); RTC_NOINIT_ATTR ones are not.
// Never initialised: a magic value tells the flag from power-on garbage.
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
RTC_NOINIT_ATTR static uint32_t rtcOpenBookAfterRestart;
static constexpr uint32_t kOpenBookMagic = 0x0B00C0DE;

bool PowerService::restartIntoLastBook() {
    if (!Platform::restartSupported() || !Platform::kMemory.restartWhenOutOfMemory) return false;
    Serial.println("[Power] Not enough memory to open the book: restarting into it.");
    rtcOpenBookAfterRestart = kOpenBookMagic;
    Platform::restart();
}

bool PowerService::takeRestartIntoBook() {
    const bool yes = rtcOpenBookAfterRestart == kOpenBookMagic;
    rtcOpenBookAfterRestart = 0;
    return yes;
}

void PowerService::handleAccidentalWakeup() {
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    // C3 wakes through the deep-sleep GPIO source, S3 through RTC ext1.
    if (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1) {
        const int8_t pPin = BoardConfig::ACTIVE.input.power;
        if (pPin >= 0) {
            const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
            pinMode(pPin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
            const int pressedLevel = activeHigh ? HIGH : LOW;

            const unsigned long requiredHoldMs = 1200;
            unsigned long startWait = millis();
            bool aborted = false;

            while (millis() - startWait < requiredHoldMs) {
                if (digitalRead(pPin) != pressedLevel) {
                    aborted = true;
                    break;
                }
                delay(10);
            }

            if (aborted) {
                Serial.println("[Power] Accidental wake-up ignored (released early). Returning to sleep...");
                freeink::PowerManager::deepSleepUntilPowerButton();
            }

            Serial.println("[Power] Power button held long enough (~1-2s). Waking up!");
            freeink::PowerManager::waitForPowerButtonRelease();
        }
    }
}

int PowerService::getSavedRtcState() {
    return rtcSavedState;
}

void PowerService::setSavedRtcState(int state) {
    rtcSavedState = state;
}

void PowerService::noteActivity() {
    AppController::getInstance().getContext().lastActivityTime = millis();
}

void PowerService::sleepIfIdle(AppContext& ctx) {
    const uint32_t idleMs = millis() - ctx.lastActivityTime;
    if (PowerPolicy::shouldEnterIdleDeepSleep(UsbMsc.active(), Battery.isCharging(), isUsbCdcConnected(),
                                              idleMs, sleepTimeoutMs())) {
        Serial.println("[Power] Idle timeout reached.");
        enterDeepSleep(ctx);
    }
}

void PowerService::idleWaitOrLightSleep(AppContext& ctx) {
    const uint32_t now = millis();
    const uint32_t idleMs = now - ctx.lastActivityTime;
    const int8_t irqPin = Buttons.touchWakeIrqPin();
    const bool irqActiveLow = Buttons.touchWakeIrqActiveLow();

    const bool gpioAlreadyAsserted = Platform::wakeInputActive(irqPin, irqActiveLow);

    LightSleepPolicy::Inputs in;
    in.featureBuilt = Platform::kDevice.touchWake;
    in.settingEnabled = ctx.config.touchWakeEnabled;
    in.touchWakeIrqPin = irqPin;
    in.wifiOff = (WiFi.getMode() == WIFI_OFF);
    in.syncBgActive = SyncService::backgroundActive();
    in.readerBgActive = Reader.backgroundActive();
    in.usbMscActive = UsbMsc.active();
    in.debouncePending = Buttons.isDebouncePending();
    in.touchPressed = Buttons.isTouchPressed();
    in.homeKeyDown = Buttons.isHomeKeyDown();
    in.usbSerialConnected = isUsbCdcConnected();
    in.irqAlreadyAsserted = gpioAlreadyAsserted;
    in.idleMs = idleMs;

    if (!LightSleepPolicy::canEnter(in)) {
        delay(10);
        return;
    }

    LightSleepPolicy::WakeTiming wt;
    wt.onBattery = !Battery.isCharging();
    wt.idleMs = idleMs;
    wt.sleepTimeoutMs = sleepTimeoutMs();
    wt.batteryCheckIntervalMs = batteryCheckIntervalMs();
    wt.msSinceBatteryCheck = now - s_lastBatteryCheck;
    wt.modalRemainingMs = Modal.remainingAutoCloseMs();
    if (ctx.currentState == STATE_READING && !Modal.isActive() && idleMs < 2500) {
        wt.waitingPrelayout = true;
        wt.msToPrelayout = 2500 - idleMs;
    }
    if (ctx.currentState == STATE_READING && ctx.config.sbClock && !Modal.isActive()) {
        wt.readingClockActive = true;
        const time_t nowSec = Platform::systemTime();
        if (nowSec > 1600000000L) {
            const uint32_t secIntoMin = static_cast<uint32_t>(nowSec % 60);
            wt.msToNextMinute = (60u - secIntoMin) * 1000u + 50u;
        }
    }

    Platform::lightSleep(LightSleepPolicy::nextWakeDelayMs(wt), irqPin, irqActiveLow);
}

void PowerService::checkBattery(AppContext& ctx) {
    const uint32_t kEveryMs = batteryCheckIntervalMs();
    constexpr int kCriticalPercent = 3;
    static int s_lowReadings = 0;
    const uint32_t now = millis();
    if (now - s_lastBatteryCheck < kEveryMs) return;
    s_lastBatteryCheck = now;
    // A failed gauge read reports 0 % and 0 V: only a real voltage counts.
    const int pct = Battery.getPercentage();
    const float volts = Battery.getVoltage();
    if (Battery.isCharging() || volts < 2.5f || pct > kCriticalPercent) {
        s_lowReadings = 0;
        return;
    }
    if (++s_lowReadings < 2) return;
    Serial.printf("[Power] Battery critical (%d%%, %.2f V): saving and going to sleep.\n", pct, volts);
    if (Reader.isOpened()) LibraryManager::getInstance().flushReaderPosition();
    drawStatusModal(tr(MsgId::POWER_LOW_BATTERY_TITLE), tr(MsgId::POWER_LOW_BATTERY_L1), tr(MsgId::POWER_LOW_BATTERY_L2));
    delay(3000);
    enterDeepSleep(ctx);
    s_lowReadings = 0;
}

void PowerService::waitOrKey(uint32_t ms) {
    Buttons.clearEvents();
    const uint32_t t0 = millis();
    ButtonEvent event;
    while (millis() - t0 < ms) {
        if (pollModalEvent(event) && event.action == ACTION_CLICK) break;
    }
    Buttons.clearEvents();
}

bool PowerService::pollModalEvent(ButtonEvent& event) {
    AppContext& ctx = AppController::getInstance().getContext();

    Buttons.update();
    if (Buttons.getEvent(event)) {
        ctx.lastActivityTime = millis();
        if (event.id == BTN_POWER && event.action == ACTION_CLICK) {
            enterDeepSleep(ctx);
            return false; // Only reached in the simulator, where deep sleep returns.
        }
        return true;
    }

    sleepIfIdle(ctx);
    delay(10);
    return false;
}

void PowerService::enterDeepSleep(AppContext& ctx) {
    Serial.println("[Power] Entering Deep Sleep. Goodnight!");
    SyncService::cancelBackground();   // a background upload in progress stays queued

    // Save active state to RTC memory across deep sleep
    rtcSavedState = ctx.currentState;
    // The frontlight screen saves on BACK: going to sleep from it keeps the level too.
    if (ctx.currentState == STATE_FRONTLIGHT_SETTINGS) ctx.config.save();

    auto& lib = LibraryManager::getInstance();

    // Ensure reading state is flushed to SD (a background layout is dropped: its file is not finished)
    Reader.stopBackground();
    if (Reader.isOpened()) {
        lib.flushReaderPosition();
    }

    // 1. Sync reading progress before sleep only if explicitly enabled and local changes exist
    if (ctx.config.syncOnSleep && ctx.config.wifiSsid.length() > 0 && ctx.config.serverHost.length() > 0 &&
        lib.lastBook.filePath.length() > 0 && lib.currentRecord().state.localDirty) {
        Serial.println("[Power] syncOnSleep enabled: uploading progress to Book Orbit...");
        drawStatusModal(tr(MsgId::POWER_SYNC_BEFORE_SLEEP), tr(MsgId::POWER_SYNC_SAVING), utf8Prefix(lib.lastBook.title, 64).c_str());
        SyncService::syncCurrentBookProgress(ctx, true);
    }

    // 2. Render Sleep Screen
    {
        using namespace uikit;
        Pass pass;
        Frame& f = pass.frame();
        clear();
        const int16_t W = width(), H = height();

        bool drewCover = false;
        if (ctx.config.coverOnSleep && lib.lastBook.filePath.length() > 0) {
            String coverPath = LibraryManager::resolveCoverPath(lib.lastBook.filePath, lib.lastBook.title, lib.lastBook.coverPath, true);
            const int16_t statusBarH = (int16_t)(lineHeight(FontBody) + 18);
            const int16_t statusBarY = (int16_t)(H - statusBarH);

            if (coverPath.length() > 0) {
                Serial.printf("[Power] Drawing sleep cover: %s (mode=%d)\n", coverPath.c_str(), ctx.config.sleepCoverFitMode);
                CoverFitMode fitMode = (ctx.config.sleepCoverFitMode == 1) ? COVER_FIT_CROP : COVER_FIT_SCALE;
                drewCover = JpegViewer::drawCover(coverPath.c_str(), 0, 0, W, statusBarY, fitMode);
            }
            if (!drewCover && lib.lastBook.title.length() > 0) {
                Serial.println("[Power] Drawing sleep cover placeholder");
                JpegViewer::drawCoverPlaceholder(lib.lastBook.title.c_str(), lib.lastBook.author.c_str(), kMargin, kMargin,
                                                 W - 2 * kMargin, statusBarY - 2 * kMargin);
                drewCover = true;
            }
            if (drewCover) {
                // Bottom status bar: "sleeping", reading progress, battery.
                Rect bar{0, statusBarY, W, statusBarH};
                fill(bar, Color::White);
                hline(0, W, statusBarY);
                Rect in = insetX(bar, kMargin);
                battery(f, Rect{(int16_t)(in.right() - 92), in.y, 92, in.height}, Color::Black);
                const char* banner = tr(MsgId::POWER_SLEEP_BANNER);
                const int16_t bw = (int16_t)(textWidth(FontBody, banner) + 16);
                text(Rect{in.x, in.y, bw, in.height}, banner, FontBody);
                if (lib.lastBook.progress > 0.0f) {
                    char progStr[32];
                    snprintf(progStr, sizeof(progStr), tr(MsgId::POWER_SLEEP_READ_FMT), (int)roundf(lib.lastBook.progress * 100.0f));
                    text(Rect{(int16_t)(in.x + bw), in.y, (int16_t)(in.width - bw - 100), in.height}, progStr, FontSmall);
                }
            }
        }

        if (!drewCover) {
            Rect box{30, (int16_t)(H / 2 - 160), (int16_t)(W - 60), 320};
            frameRect(box, Color::Black, 2);
            Rect in = insetXY(box, 20, 30);
            text(takeTop(in, (int16_t)(lineHeight(FontLarge) + 8)), deviceDisplayName(), FontLarge, Color::Black, TextAlign::Center);
            text(takeTop(in, (int16_t)(lineHeight(FontLarge) + 8)), tr(MsgId::POWER_SLEEP_BANNER), FontLarge, Color::Black, TextAlign::Center);
            takeTop(in, 16);
            hline((int16_t)(in.x + 20), (int16_t)(in.right() - 20), in.y);
            takeTop(in, 24);
            text(takeTop(in, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::POWER_WAKE_HINT_L1), FontBody, Color::Black, TextAlign::Center);
            text(takeTop(in, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::POWER_WAKE_HINT_L2), FontBody, Color::Black, TextAlign::Center);
        }
    }

    Display.update(REFRESH_FULL);
    Display.waitSync();

    // Power down display controller
    Display.sleep();

    // Flush and shut down SD card / storage cleanly before cutting power rails
    Storage.shutdown();

    // Turn off Wi-Fi
    BookOrbit.disconnectWiFi();

    // Power down via FreeInk PowerManager
    freeink::PowerManager::waitForPowerButtonRelease();
    freeink::PowerManager::armPowerButtonWakeup();
    Frontlight.prepareForSleep();
    BoardImu.sleep();
    freeink::PowerManager::powerDownRailsForSleep();
    freeink::PowerManager::deepSleep();

    // On hardware deepSleep() never returns (wake-up is a reset). In the desktop
    // simulator it does, so restart the idle timer to avoid re-entering sleep
    // on every poll.
    ctx.lastActivityTime = millis();
}
