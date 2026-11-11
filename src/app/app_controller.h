#pragma once

#include <Arduino.h>
#include "app_state.h"
#include "../core/auto_rotate.h"
#include "../platform/hal/buttons.h"

// Top-level application loop: boot sequence, event routing and power rules.
// Screen-specific behaviour lives in app/screens/*; dialogs in ui/modal.h.
class AppController {
public:
    static AppController& getInstance() {
        static AppController instance;
        return instance;
    }

    void setup();
    void loop();

    AppContext& getContext() { return _ctx; }

    // Puts the loaded config into effect: language, frontlight, the reader's font,
    // layout, refresh, pictures, orientation and status bar, the library filter.
    // At boot and after "Reload from SD" (an open book is laid out again once).
    void applyConfig();

    // Redraws the screen belonging to the current AppState.
    void redrawCurrentScreen(bool fullRefresh);

    void resetAutoRotate() { _autoRotate.reset(); }

private:
    AppController() = default;

    void dispatch(const ButtonEvent& event);

    AppContext _ctx;
    AutoRotateTracker _autoRotate;
    uint32_t _lastImuPollMs = 0;
    uint32_t _lastClockCheckMs = 0;
    int32_t _lastClockMinute = -1;
};
