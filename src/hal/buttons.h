#pragma once

#include <Arduino.h>
#include <InputManager.h>

enum ButtonId {
    BTN_NONE = 0,
    BTN_POWER,
    BTN_CONFIRM,
    BTN_BACK,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_UP,
    BTN_DOWN,
    BTN_TAP,     // touch tap; x/y hold the logical (portrait) position
    // Touch swipes, named by the finger's direction; x/y = start point.
    BTN_SWIPE_LEFT,
    BTN_SWIPE_RIGHT,
    BTN_SWIPE_UP,
    BTN_SWIPE_DOWN
};

enum ButtonAction {
    ACTION_NONE = 0,
    ACTION_PRESS,
    ACTION_CLICK,
    ACTION_LONG_PRESS
};

struct ButtonEvent {
    ButtonId id;
    ButtonAction action;
    int16_t x = 0;   // BTN_TAP only
    int16_t y = 0;
};

class ButtonManager {
public:
    ButtonManager();

    void begin();
    void update();
    bool getEvent(ButtonEvent& event);
    bool isPressed(ButtonId id);
    void clearEvents();
    static const char* getName(ButtonId id);

    // Side slide switch that turns the frontlight on/off (Picco, GPIO6).
    bool hasLightSwitch() const { return _freeinkInput.hasToggleSwitch(); }
    bool lightSwitchOn() const { return _freeinkInput.isToggleSwitchOn(); }
    // True once after the switch moved; consumed by the call.
    bool takeLightSwitchChange() { bool c = _lightSwitchChanged; _lightSwitchChanged = false; return c; }

private:
    InputManager _freeinkInput;
    ButtonEvent _pendingEvent;
    bool _hasPendingEvent;
    bool _powerLongPressFired;
    bool _lightSwitchChanged = false;
};

extern ButtonManager Buttons;
