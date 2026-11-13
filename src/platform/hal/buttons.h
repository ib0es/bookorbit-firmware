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
    ButtonId id = BTN_NONE;
    ButtonAction action = ACTION_NONE;
    int16_t x = 0;   // BTN_TAP only
    int16_t y = 0;
    // 0 for a press; 1, 2, 3... for the clicks an arrow key repeats while held
    // (long lists move a page at a time after a few).
    uint8_t repeat = 0;
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

    // Maps a touch given in panel coordinates normalised to 0..1 to logical
    // pixels of the UI (orientation and size are the UI's business). Taps and
    // swipes are reported only once one is set.
    using TouchMapper = void (*)(float nx, float ny, int16_t& x, int16_t& y);
    void setTouchMapper(TouchMapper mapper) { _touchMapper = mapper; }

    // Which key an event reports, given the physical key: the app turns the
    // front keys with the screen. Applied by getEvent(), so every input loop
    // (main loop, dialogs, keyboard, network list) sees the same keys;
    // isPressed() stays physical.
    using KeyRemap = ButtonId (*)(ButtonId physical);
    void setKeyRemap(KeyRemap remap) { _keyRemap = remap; }

    // A touch-only board whose touch panel did not answer has UP, DOWN and
    // POWER left: then a held DOWN is OK and a held UP is BACK (C4). UP and DOWN
    // report their click on release (a hold is told from a click), and do not
    // repeat.
    void setHoldFallback(bool on) { _holdFallback = on; }
    bool holdFallback() const { return _holdFallback; }

    // Side slide switch that turns the frontlight on/off (Picco, GPIO6).
    bool hasLightSwitch() const { return _freeinkInput.hasToggleSwitch(); }
    bool lightSwitchOn() const { return _freeinkInput.isToggleSwitchOn(); }
    // True once after the switch moved; consumed by the call.
    bool takeLightSwitchChange() { bool c = _lightSwitchChanged; _lightSwitchChanged = false; return c; }

    bool hasTouch() const { return _freeinkInput.hasTouch(); }
    bool hasHomeKey() const;
    bool isTouchPressed() const { return _freeinkInput.isTouchPressed(); }
    bool isHomeKeyDown() const { return _freeinkInput.isHomeKeyDown(); }
    bool isDebouncePending() const { return _freeinkInput.isDebouncePending(); }
    int8_t touchWakeIrqPin() const { return _freeinkInput.touchWakeIrqPin(); }
    bool touchWakeIrqActiveLow() const { return _freeinkInput.touchWakeIrqActiveLow(); }

private:
    InputManager _freeinkInput;
    TouchMapper _touchMapper = nullptr;
    KeyRemap _keyRemap = nullptr;
    bool _holdFallback = false;
    ButtonId _holdKey = BTN_NONE;   // UP or DOWN down, in the hold fallback
    uint32_t _holdSince = 0;
    bool _holdFired = false;
    ButtonEvent _pendingEvent;
    bool _hasPendingEvent;
    bool _powerLongPressFired;
    bool _lightSwitchChanged = false;
    // Auto-repeat of a held arrow key.
    ButtonId _heldKey = BTN_NONE;
    uint32_t _heldSince = 0, _lastRepeat = 0;
    uint8_t _repeats = 0;
};

extern ButtonManager Buttons;
