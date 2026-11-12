#include "power_service.h"
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
#include "../ui/i18n.h"
#include "../core/ui_port.h"
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

RTC_NOINIT_ATTR static uint32_t rtcSettingsAfterRestart;
static constexpr uint32_t kSettingsMagic = 0x5E77C0DE;

void PowerService::restartIntoSettings() {
    rtcSettingsAfterRestart = kSettingsMagic;
    Platform::restart();
}

bool PowerService::takeRestartIntoSettings() {
    const bool yes = rtcSettingsAfterRestart == kSettingsMagic;
    rtcSettingsAfterRestart = 0;
    return yes;
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
    wt.modalRemainingMs = Ui::modalRemainingMs();
    if (ctx.currentState == STATE_READING && !Ui::modalActive() && idleMs < 2500) {
        wt.waitingPrelayout = true;
        wt.msToPrelayout = 2500 - idleMs;
    }
    if (ctx.currentState == STATE_READING && ctx.config.sbClock && !Ui::modalActive()) {
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
    Ui::status(tr(MsgId::POWER_LOW_BATTERY_TITLE), tr(MsgId::POWER_LOW_BATTERY_L1), tr(MsgId::POWER_LOW_BATTERY_L2));
    delay(3000);
    enterDeepSleep(ctx, /*syncAllowed=*/false);   // B10: no radio on an almost flat battery
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

void PowerService::enterDeepSleep(AppContext& ctx, bool syncAllowed) {
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
    // (never on a critical battery: the progress stays queued for the next sync)
    if (!syncAllowed) Serial.println("[Power] Critical battery: no sync before sleep; progress stays queued.");
    if (syncAllowed && ctx.config.syncOnSleep && ctx.config.wifiSsid.length() > 0 && ctx.config.serverHost.length() > 0 &&
        lib.lastBook.filePath.length() > 0 && lib.currentRecord().state.localDirty) {
        Serial.println("[Power] syncOnSleep enabled: uploading progress to Book Orbit...");
        Ui::status(tr(MsgId::POWER_SYNC_BEFORE_SLEEP), tr(MsgId::POWER_SYNC_SAVING), utf8Prefix(lib.lastBook.title, 64).c_str());
        SyncService::syncCurrentBookProgress(ctx, true);
    }

    // 2. Render Sleep Screen (drawn by the UI: A5)
    {
        SleepScreen s;
        String coverPath;
        s.withCover = ctx.config.coverOnSleep && lib.lastBook.filePath.length() > 0;
        if (s.withCover) {
            coverPath = LibraryManager::resolveCoverPath(lib.lastBook.filePath, lib.lastBook.title, lib.lastBook.coverPath, true);
            if (coverPath.length() > 0) {
                Serial.printf("[Power] Drawing sleep cover: %s (mode=%d)\n", coverPath.c_str(), ctx.config.sleepCoverFitMode);
            }
        }
        s.coverPath = coverPath.c_str();
        s.coverCrop = ctx.config.sleepCoverFitMode == 1;
        s.title = lib.lastBook.title.c_str();
        s.author = lib.lastBook.author.c_str();
        s.progress = lib.lastBook.progress;
        Ui::sleepScreen(s);
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
