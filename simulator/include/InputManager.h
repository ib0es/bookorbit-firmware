#pragma once

#include <cstdint>
#include <cstring>
#include "Arduino.h"

void simPumpEvents();
// Called once per input poll (headless runner feeds nested input loops from it).
void simInputPolled();

class InputManager {
public:
    enum Button {
        BTN_POWER = 0,
        BTN_CONFIRM,
        BTN_BACK,
        BTN_LEFT,
        BTN_RIGHT,
        BTN_UP,
        BTN_DOWN,
        BTN_COUNT
    };

    InputManager() {
        memset(_pressed, 0, sizeof(_pressed));
        memset(_prevPressed, 0, sizeof(_prevPressed));
        _powerPressStart = 0;
    }

    void begin() {}

    // Slide switch (Picco): none in the simulator.
    bool hasToggleSwitch() const { return false; }
    bool isToggleSwitchOn() const { return true; }
    bool wasToggleSwitchChanged() const { return false; }

    void update() {
        simPumpEvents();
        simInputPolled();
        _tapNow = s_tapQueued; _tapNowX = s_tapX; _tapNowY = s_tapY; s_tapQueued = false;
        _swipeNow = s_swipeQueued; for (int k = 0; k < 4; k++) _swipeNowP[k] = s_swipeP[k]; s_swipeQueued = false;
        _homeTapNow = s_homeTapQueued; s_homeTapQueued = false;
        _homeLongNow = s_homeLongQueued; s_homeLongQueued = false;
        for (int i = 0; i < BTN_COUNT; i++) {
            _prevPressed[i] = _pressed[i];
            _pressed[i] = s_simState[i];
        }
        if (_pressed[BTN_POWER] && !_prevPressed[BTN_POWER]) {
            _powerPressStart = millis();
        } else if (!_pressed[BTN_POWER]) {
            _powerPressStart = 0;
        }
    }

    bool wasPressed(Button btn) const {
        if (btn < 0 || btn >= BTN_COUNT) return false;
        return _pressed[btn] && !_prevPressed[btn];
    }

    bool isPressed(Button btn) const {
        if (btn < 0 || btn >= BTN_COUNT) return false;
        return _pressed[btn];
    }

    bool wasReleased(Button btn) const {
        if (btn < 0 || btn >= BTN_COUNT) return false;
        return !_pressed[btn] && _prevPressed[btn];
    }

    uint32_t getPowerButtonHeldTime() const {
        if (!_pressed[BTN_POWER] || _powerPressStart == 0) return 0;
        return millis() - _powerPressStart;
    }

    // Static hooks for SDL2 keyboard loop
    static void simPress(Button btn) {
        if (btn >= 0 && btn < BTN_COUNT) s_simState[btn] = true;
    }

    static void simRelease(Button btn) {
        if (btn >= 0 && btn < BTN_COUNT) s_simState[btn] = false;
    }

    static void simClick(Button btn) {
        if (btn >= 0 && btn < BTN_COUNT) {
            s_simState[btn] = true;
            s_oneShotClicks[btn] = true;
        }
    }

    static bool s_simState[BTN_COUNT];
    static bool s_oneShotClicks[BTN_COUNT];

    // Touch: a tap in normalized panel-native coordinates (like the real
    // InputManager), queued by the headless script ('T' x,y) and reported by
    // the next update().
#if (defined(FREEINK_CAP_TOUCH) && FREEINK_CAP_TOUCH) || (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) || \
    (defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO)
    static inline bool s_touchEnabled = true;
#else
    static inline bool s_touchEnabled = false;
#endif
    static inline bool s_tapQueued = false;
    static inline float s_tapX = 0, s_tapY = 0;
    static void simTap(float nx, float ny) { s_touchEnabled = true; s_tapQueued = true; s_tapX = nx; s_tapY = ny; }
    bool hasTouch() const { return s_touchEnabled; }
    bool isTouchPressed() const { return false; }
    bool isDebouncePending() const { return false; }
    bool wasTouchTap(float& nx, float& ny) const {
        if (!_tapNow) return false;
        nx = _tapNowX; ny = _tapNowY;
        return true;
    }

    // GT911 capacitive Home Key & INT light-sleep wake (X4 Pro).
    static inline bool s_homeTapQueued = false;
    static inline bool s_homeLongQueued = false;
    static inline bool s_homeDown = false;
    static void simHomeKeyTap() { s_homeTapQueued = true; }
    static void simHomeKeyLongPress() { s_homeLongQueued = true; }
    bool wasHomeKeyTapped() const { return _homeTapNow; }
    bool wasHomeKeyLongPressed() const { return _homeLongNow; }
    bool isHomeKeyDown() const { return s_homeDown; }
    int8_t touchWakeIrqPin() const {
#if defined(FREEINK_GT911_INT_WAKE) && FREEINK_GT911_INT_WAKE
        return 10;
#else
        return -1;
#endif
    }
    bool touchWakeIrqActiveLow() const { return true; }

    // Swipe in normalized panel-native coordinates ('W' in the headless script).
    static inline bool s_swipeQueued = false;
    static inline float s_swipeP[4] = {0, 0, 0, 0};
    static void simSwipe(float x0, float y0, float x1, float y1) {
        s_touchEnabled = true; s_swipeQueued = true;
        s_swipeP[0] = x0; s_swipeP[1] = y0; s_swipeP[2] = x1; s_swipeP[3] = y1;
    }
    bool wasSwipe(float& x0, float& y0, float& x1, float& y1) const {
        if (!_swipeNow) return false;
        x0 = _swipeNowP[0]; y0 = _swipeNowP[1]; x1 = _swipeNowP[2]; y1 = _swipeNowP[3];
        return true;
    }

private:
    bool _swipeNow = false;
    float _swipeNowP[4] = {0, 0, 0, 0};
    bool _pressed[BTN_COUNT];
    bool _prevPressed[BTN_COUNT];
    uint32_t _powerPressStart;
    bool _tapNow = false;
    float _tapNowX = 0, _tapNowY = 0;
    bool _homeTapNow = false;
    bool _homeLongNow = false;
};
