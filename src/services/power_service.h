#pragma once

#include <Arduino.h>
#include "../app/app_state.h"
#include "../hal/buttons.h"

class PowerService {
public:
    static void handleAccidentalWakeup();
    static int getSavedRtcState();
    static void setSavedRtcState(int state);
    static void enterDeepSleep(AppContext& ctx);

    // Button polling for blocking UI loops (modals, on-screen keyboard, Wi-Fi picker,
    // web portal). Applies the same power rules as AppController::loop():
    //   * POWER click            -> deep sleep;
    //   * idle >= SLEEP_TIMEOUT_MS on battery -> deep sleep.
    // Any received event counts as user activity. Includes a short delay, so callers
    // can use it directly as the body of their wait loop.
    // Returns true when a (non-POWER) event was received.
    static bool pollModalEvent(ButtonEvent& event);

    // Marks user activity (e.g. an HTTP request to the config portal) so the idle
    // timer does not fire while the user is interacting by other means.
    static void noteActivity();

    // Enters deep sleep if the device has been idle for SLEEP_TIMEOUT_MS on battery.
    static void sleepIfIdle(AppContext& ctx);

    // Once a minute: at a critical charge (two readings in a row, on battery)
    // the position is saved and the device goes to sleep with a message.
    static void checkBattery(AppContext& ctx);
};
