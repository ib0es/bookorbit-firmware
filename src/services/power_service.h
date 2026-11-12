#pragma once

#include <Arduino.h>
#include "../app/app_state.h"
#include "../platform/hal/buttons.h"

class PowerService {
public:
    // The app's context, for the blocking loops that note activity and may
    // put the device to sleep (set once at start-up: services do not reach
    // into the app controller).
    static void setContext(AppContext& ctx);
    static void handleAccidentalWakeup();
    static int getSavedRtcState();

    // A book that could not be opened for want of memory (the heap is cut up
    // after Wi-Fi): the device restarts and opens it right away (lastBook), as it
    // does after sleep. False on the simulator (no restart there).
    static bool restartIntoLastBook();
    // True once, at the boot that restartIntoLastBook() caused.
    static bool takeRestartIntoBook();
    // A restart that comes back to Settings, not a cold boot (after USB mass
    // storage: the PHY goes back to Serial/JTAG only through a reset). B7.
    [[noreturn]] static void restartIntoSettings();
    static bool takeRestartIntoSettings();
    // `syncAllowed` false: no sync before sleep, whatever the settings (an almost
    // flat battery must not power the radio: B10).
    static void enterDeepSleep(AppContext& ctx, bool syncAllowed = true);

    // Button polling for blocking UI loops (modals, on-screen keyboard, Wi-Fi picker,
    // web portal). Applies the same power rules as AppController::loop():
    //   * POWER click            -> deep sleep;
    //   * idle >= SLEEP_TIMEOUT_MS on battery -> deep sleep.
    // Any received event counts as user activity. Includes a short delay, so callers
    // can use it directly as the body of their wait loop.
    // Returns true when a (non-POWER) event was received.
    static bool pollModalEvent(ButtonEvent& event);

    // Leaves a status message up for `ms`, or until a key is pressed (the key is
    // taken: it closes the message, it does not act on the screen below).
    static void waitOrKey(uint32_t ms);

    // Marks user activity (e.g. an HTTP request to the config portal) so the idle
    // timer does not fire while the user is interacting by other means.
    static void noteActivity();

    // Enters deep sleep if the device has been idle for SLEEP_TIMEOUT_MS on battery.
    static void sleepIfIdle(AppContext& ctx);

    // Sleeps cleanly between loop ticks: on builds/configs with GT911 INT wake
    // enabled and when safe, enters bounded ESP32-S3 light sleep; otherwise waits ~10 ms.
    static void idleWaitOrLightSleep(AppContext& ctx);

    // Once a minute: at a critical charge (two readings in a row, on battery)
    // the position is saved and the device goes to sleep with a message.
    static void checkBattery(AppContext& ctx);
};
