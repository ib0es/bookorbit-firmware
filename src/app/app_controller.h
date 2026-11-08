#pragma once

#include <Arduino.h>
#include "app_state.h"
#include "../hal/buttons.h"

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

    // Redraws the screen belonging to the current AppState.
    void redrawCurrentScreen(bool fullRefresh);

private:
    AppController() = default;

    void dispatch(const ButtonEvent& event);

    AppContext _ctx;
};
