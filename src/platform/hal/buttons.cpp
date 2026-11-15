#include "buttons.h"
#include <BoardConfig.h>
#include "../device.h"

ButtonManager Buttons;

ButtonManager::ButtonManager()
    : _pendingEvent{BTN_NONE, ACTION_NONE}, _hasPendingEvent(false), _powerLongPressFired(false) {}

bool ButtonManager::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

void ButtonManager::setHoldFallback(bool on) {
#if defined(FREEINK_DEVICE_PAPERMONO) && FREEINK_DEVICE_PAPERMONO
    _holdFallback = false;
#else
    _holdFallback = on;
#endif
}

void ButtonManager::setSleepHoldSec(int sec) {
    _sleepHoldSec = sec < 0 ? 0 : sec;
    _powerHoldActive = false;
}

void ButtonManager::begin() {
    _freeinkInput.begin();
    Serial.printf("[Buttons] FreeInk InputManager initialized, touch: %s, homeKey: %s\n",
                  _freeinkInput.hasTouch() ? "on" : "off", hasHomeKey() ? "on" : "off");
    if (_freeinkInput.hasToggleSwitch())
        Serial.printf("[Buttons] Light switch at boot: %s\n", _freeinkInput.isToggleSwitchOn() ? "on" : "off");
#if defined(FREEINK_GT911_INT_WAKE) && FREEINK_GT911_INT_WAKE
    Serial.printf("[Buttons] GT911 INT wake: %s (pin=%d, active=%s)\n",
                  _freeinkInput.touchWakeIrqPin() >= 0 ? "armed" : "unavailable", (int)_freeinkInput.touchWakeIrqPin(),
                  _freeinkInput.touchWakeIrqActiveLow() ? "LOW" : "HIGH");
#endif
}

void ButtonManager::update() {
    _freeinkInput.update();

    // The switch edge is one-shot per update(): latch it before the early return.
    if (_freeinkInput.wasToggleSwitchChanged()) {
        _lightSwitchChanged = true;
        Serial.printf("[Buttons] Light switch: %s\n", _freeinkInput.isToggleSwitchOn() ? "on" : "off");
    }

    // A held arrow key repeats: after 0.5 s, then every 0.25 s. The single event
    // slot throttles it to the screen's pace: a click repeated while the panel
    // refreshes waits for the next update().
    if (_heldKey != BTN_NONE && !isPressed(_heldKey)) {
        _heldKey = BTN_NONE;
        // A repeat queued during a blocking refresh expires with the hold.
        // Preserve real presses: they must still be delivered after release.
        if (_hasPendingEvent && _pendingEvent.repeat > 0) _hasPendingEvent = false;
    }
    if (_hasPendingEvent) return; // Keep pending until consumed

    // Hold fallback (no touch panel): UP / DOWN pressed are timed here.
    if (_holdKey != BTN_NONE) {
        constexpr uint32_t kHoldMs = 600;
        if (!isPressed(_holdKey)) {
            if (!_holdFired) {   // released before the hold: an ordinary click
                _pendingEvent = {_holdKey, ACTION_CLICK};
                _hasPendingEvent = true;
                Serial.printf("[Buttons] Event: CLICK %s (released)\n", getName(_holdKey));
            }
            _holdKey = BTN_NONE;
            if (_hasPendingEvent) return;
        } else {
            if (!_holdFired && millis() - _holdSince >= kHoldMs) {
                _holdFired = true;
                const ButtonId as = _holdKey == BTN_DOWN ? BTN_CONFIRM : BTN_BACK;
                _pendingEvent = {as, ACTION_CLICK};
                _hasPendingEvent = true;
                Serial.printf("[Buttons] Event: HOLD %s -> %s (no touch)\n", getName(_holdKey), getName(as));
            }
            return;
        }
    }

    if (_sharedPowerKey != BTN_NONE) {
        const uint32_t kHoldMs = _sleepHoldSec > 0 ? (uint32_t)_sleepHoldSec * 1000 : 600;
        if (!isPressed(_sharedPowerKey)) {
            if (!_sharedPowerFired) {
                _pendingEvent = {_sharedPowerKey, ACTION_CLICK};
                _hasPendingEvent = true;
                Serial.printf("[Buttons] Event: CLICK %s (shared)\n", getName(_sharedPowerKey));
            }
            _sharedPowerKey = BTN_NONE;
            if (_hasPendingEvent) return;
        } else {
            if (!_sharedPowerFired && millis() - _sharedPowerSince >= kHoldMs) {
                _sharedPowerFired = true;
                _pendingEvent = {BTN_POWER, ACTION_CLICK};
                _hasPendingEvent = true;
                Serial.printf("[Buttons] Event: HOLD %s -> POWER (shared)\n", getName(_sharedPowerKey));
            }
            return;
        }
    }

    if (_powerHoldActive) {
        if (!isPressed(BTN_POWER)) {
            _powerHoldActive = false;
            Serial.println("[Buttons] Power button released before hold duration");
        } else if (millis() - _powerHoldSince >= (uint32_t)_sleepHoldSec * 1000) {
            _powerHoldActive = false;
            _pendingEvent = {BTN_POWER, ACTION_CLICK};
            _hasPendingEvent = true;
            Serial.printf("[Buttons] Event: HOLD POWER (%d s)\n", _sleepHoldSec);
            return;
        }
    }

    const bool freshArrow =
            _freeinkInput.wasPressed(InputManager::BTN_UP) || _freeinkInput.wasPressed(InputManager::BTN_DOWN) ||
            _freeinkInput.wasPressed(InputManager::BTN_LEFT) || _freeinkInput.wasPressed(InputManager::BTN_RIGHT);
    if (_heldKey != BTN_NONE && !freshArrow && !_freeinkInput.isDebouncePending()) {
        const uint32_t now = millis();
        if (now - _heldSince >= 500 && now - _lastRepeat >= 250) {
            _lastRepeat = now;
            if (_repeats < 255) _repeats++;
            _pendingEvent = {_heldKey, ACTION_CLICK};
            _pendingEvent.repeat = _repeats;
            _hasPendingEvent = true;
            return;
        }
    }

    // Check click / press edges
    if (Platform::kDevice.powerSharedWithConfirm &&
        (_freeinkInput.wasPressed(InputManager::BTN_CONFIRM) || _freeinkInput.wasPressed(InputManager::BTN_POWER))) {
        _sharedPowerKey = BTN_CONFIRM;
        _sharedPowerSince = millis();
        _sharedPowerFired = false;
    } else if (Platform::kDevice.powerSharedWithBack && (_freeinkInput.wasPressed(InputManager::BTN_BACK) ||
                                                         _freeinkInput.wasPressed(InputManager::BTN_POWER))) {
        _sharedPowerKey = BTN_BACK;
        _sharedPowerSince = millis();
        _sharedPowerFired = false;
    } else if (_freeinkInput.wasPressed(InputManager::BTN_CONFIRM)) {
        _pendingEvent = {BTN_CONFIRM, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK CONFIRM");
    } else if (_holdFallback &&
               (_freeinkInput.wasPressed(InputManager::BTN_UP) || _freeinkInput.wasPressed(InputManager::BTN_DOWN))) {
        _holdKey = _freeinkInput.wasPressed(InputManager::BTN_DOWN) ? BTN_DOWN : BTN_UP;
        _holdSince = millis();
        _holdFired = false;
    } else if (_freeinkInput.wasPressed(InputManager::BTN_UP)) {
        _pendingEvent = {BTN_UP, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK UP");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_DOWN)) {
        _pendingEvent = {BTN_DOWN, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK DOWN");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_LEFT)) {
        _pendingEvent = {BTN_LEFT, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK LEFT");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_RIGHT)) {
        _pendingEvent = {BTN_RIGHT, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK RIGHT");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_BACK)) {
        _pendingEvent = {BTN_BACK, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK BACK");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_POWER)) {
        if (_sleepHoldSec <= 0) {
            _pendingEvent = {BTN_POWER, ACTION_CLICK};
            _hasPendingEvent = true;
            Serial.println("[Buttons] Event: CLICK POWER");
        } else {
            _powerHoldActive = true;
            _powerHoldSince = millis();
            Serial.printf("[Buttons] Power button pressed, waiting %d s hold...\n", _sleepHoldSec);
        }
    } else if (_freeinkInput.wasHomeKeyTapped()) {
        _pendingEvent = {BTN_BACK, ACTION_CLICK};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: HOME_KEY TAP -> BACK");
    } else if (_freeinkInput.wasHomeKeyLongPressed()) {
        _pendingEvent = {BTN_BACK, ACTION_LONG_PRESS};
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: HOME_KEY LONG -> READER MENU");
    } else {
        float nx, ny;
        if (_touchMapper && _freeinkInput.hasTouch() && _freeinkInput.wasTouchTap(nx, ny)) {
            // Panel-native normalized -> logical portrait pixels of the UI.
            struct {
                int16_t x, y;
            } p;
            _touchMapper(nx, ny, p.x, p.y);
            _pendingEvent = {BTN_TAP, ACTION_CLICK, p.x, p.y};
            _hasPendingEvent = true;
            Serial.printf("[Buttons] Event: TAP %d,%d\n", p.x, p.y);
        } else if (_touchMapper && _freeinkInput.hasTouch()) {
            float x0, y0, x1, y1;
            if (_freeinkInput.wasSwipe(x0, y0, x1, y1)) {
                struct {
                    int16_t x, y;
                } a, b;
                _touchMapper(x0, y0, a.x, a.y);
                _touchMapper(x1, y1, b.x, b.y);
                const int dx = b.x - a.x, dy = b.y - a.y;
                ButtonId id;
                if (abs(dx) >= abs(dy))
                    id = dx < 0 ? BTN_SWIPE_LEFT : BTN_SWIPE_RIGHT;
                else
                    id = dy < 0 ? BTN_SWIPE_UP : BTN_SWIPE_DOWN;
                _pendingEvent = {id, ACTION_CLICK, a.x, a.y};
                _hasPendingEvent = true;
                Serial.printf("[Buttons] Event: %s %d,%d -> %d,%d\n", getName(id), a.x, a.y, b.x, b.y);
            }
        }
    }
}

bool ButtonManager::getEvent(ButtonEvent& event) {
    if (_hasPendingEvent) {
        event = _pendingEvent;
        _hasPendingEvent = false;
        // A fresh press of an arrow key starts its auto-repeat clock.
        const ButtonId id = event.id;
        if (event.repeat == 0 && !_holdFallback &&
            (id == BTN_UP || id == BTN_DOWN || id == BTN_LEFT || id == BTN_RIGHT)) {
            _heldKey = id;
            _heldSince = _lastRepeat = millis();
            _repeats = 0;
        } else if (event.repeat == 0) {
            _heldKey = BTN_NONE;
        }
        if (_keyRemap) event.id = _keyRemap(event.id);
        return true;
    }
    return false;
}

bool ButtonManager::isPressed(ButtonId id) {
    switch (id) {
        case BTN_POWER:
            return _freeinkInput.isPressed(InputManager::BTN_POWER);
        case BTN_CONFIRM:
            return _freeinkInput.isPressed(InputManager::BTN_CONFIRM);
        case BTN_BACK:
            return _freeinkInput.isPressed(InputManager::BTN_BACK) || _freeinkInput.isHomeKeyDown();
        case BTN_LEFT:
            return _freeinkInput.isPressed(InputManager::BTN_LEFT);
        case BTN_RIGHT:
            return _freeinkInput.isPressed(InputManager::BTN_RIGHT);
        case BTN_UP:
            return _freeinkInput.isPressed(InputManager::BTN_UP);
        case BTN_DOWN:
            return _freeinkInput.isPressed(InputManager::BTN_DOWN);
        default:
            return false;
    }
}

void ButtonManager::clearEvents() {
    _hasPendingEvent = false;
    _heldKey = BTN_NONE;
    _holdKey = BTN_NONE;
    _sharedPowerKey = BTN_NONE;
    _pendingEvent = {BTN_NONE, ACTION_NONE};
}

const char* ButtonManager::getName(ButtonId id) {
    switch (id) {
        case BTN_POWER:
            return "POWER";
        case BTN_CONFIRM:
            return "CONFIRM";
        case BTN_BACK:
            return "BACK";
        case BTN_LEFT:
            return "LEFT";
        case BTN_RIGHT:
            return "RIGHT";
        case BTN_UP:
            return "UP";
        case BTN_DOWN:
            return "DOWN";
        case BTN_TAP:
            return "TAP";
        case BTN_SWIPE_LEFT:
            return "SWIPE_LEFT";
        case BTN_SWIPE_RIGHT:
            return "SWIPE_RIGHT";
        case BTN_SWIPE_UP:
            return "SWIPE_UP";
        case BTN_SWIPE_DOWN:
            return "SWIPE_DOWN";
        default:
            return "NONE";
    }
}
