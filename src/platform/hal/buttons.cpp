#include "buttons.h"
#include <BoardConfig.h>
#include "../../ui/kit/ui_kit.h"

ButtonManager Buttons;

ButtonManager::ButtonManager() : _hasPendingEvent(false), _powerLongPressFired(false) {
    _pendingEvent = { BTN_NONE, ACTION_NONE };
}

bool ButtonManager::hasHomeKey() const {
    return BoardConfig::hasHomeKey();
}

void ButtonManager::begin() {
    _freeinkInput.begin();
    Serial.printf("[Buttons] FreeInk InputManager initialized, touch: %s, homeKey: %s\n",
                  _freeinkInput.hasTouch() ? "on" : "off",
                  hasHomeKey() ? "on" : "off");
    if (_freeinkInput.hasToggleSwitch())
        Serial.printf("[Buttons] Light switch at boot: %s\n", _freeinkInput.isToggleSwitchOn() ? "on" : "off");
#if defined(FREEINK_GT911_INT_WAKE) && FREEINK_GT911_INT_WAKE
    Serial.printf("[Buttons] GT911 INT wake: %s (pin=%d, active=%s)\n",
                  _freeinkInput.touchWakeIrqPin() >= 0 ? "armed" : "unavailable",
                  (int)_freeinkInput.touchWakeIrqPin(),
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
    if (_heldKey != BTN_NONE && !isPressed(_heldKey)) _heldKey = BTN_NONE;
    if (_hasPendingEvent) return; // Keep pending until consumed
    if (_heldKey != BTN_NONE) {
        const uint32_t now = millis();
        if (now - _heldSince >= 500 && now - _lastRepeat >= 250) {
            _lastRepeat = now;
            if (_repeats < 255) _repeats++;
            _pendingEvent = { _heldKey, ACTION_CLICK };
            _pendingEvent.repeat = _repeats;
            _hasPendingEvent = true;
            return;
        }
    }

    // Check click / press edges
    if (_freeinkInput.wasPressed(InputManager::BTN_CONFIRM)) {
        _pendingEvent = { BTN_CONFIRM, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK CONFIRM");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_UP)) {
        _pendingEvent = { BTN_UP, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK UP");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_DOWN)) {
        _pendingEvent = { BTN_DOWN, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK DOWN");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_LEFT)) {
        _pendingEvent = { BTN_LEFT, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK LEFT");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_RIGHT)) {
        _pendingEvent = { BTN_RIGHT, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK RIGHT");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_BACK)) {
        _pendingEvent = { BTN_BACK, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK BACK");
    } else if (_freeinkInput.wasPressed(InputManager::BTN_POWER)) {
        _pendingEvent = { BTN_POWER, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: CLICK POWER");
    } else if (_freeinkInput.wasHomeKeyTapped()) {
        _pendingEvent = { BTN_BACK, ACTION_CLICK };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: HOME_KEY TAP -> BACK");
    } else if (_freeinkInput.wasHomeKeyLongPressed()) {
        _pendingEvent = { BTN_BACK, ACTION_LONG_PRESS };
        _hasPendingEvent = true;
        Serial.println("[Buttons] Event: HOME_KEY LONG -> READER MENU");
    } else {
        float nx, ny;
        if (_freeinkInput.hasTouch() && _freeinkInput.wasTouchTap(nx, ny)) {
            // Panel-native normalized -> logical portrait pixels of the UI.
            const freeink::ui::Point p = freeink::ui::touchToLogical(uikit::device(), nx, ny);
            _pendingEvent = { BTN_TAP, ACTION_CLICK, p.x, p.y };
            _hasPendingEvent = true;
            Serial.printf("[Buttons] Event: TAP %d,%d\n", p.x, p.y);
        } else if (_freeinkInput.hasTouch()) {
            float x0, y0, x1, y1;
            if (_freeinkInput.wasSwipe(x0, y0, x1, y1)) {
                const freeink::ui::Point a = freeink::ui::touchToLogical(uikit::device(), x0, y0);
                const freeink::ui::Point b = freeink::ui::touchToLogical(uikit::device(), x1, y1);
                const int dx = b.x - a.x, dy = b.y - a.y;
                ButtonId id;
                if (abs(dx) >= abs(dy)) id = dx < 0 ? BTN_SWIPE_LEFT : BTN_SWIPE_RIGHT;
                else                    id = dy < 0 ? BTN_SWIPE_UP : BTN_SWIPE_DOWN;
                _pendingEvent = { id, ACTION_CLICK, a.x, a.y };
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
        if (event.repeat == 0 && (id == BTN_UP || id == BTN_DOWN || id == BTN_LEFT || id == BTN_RIGHT)) {
            _heldKey = id;
            _heldSince = _lastRepeat = millis();
            _repeats = 0;
        } else if (event.repeat == 0) {
            _heldKey = BTN_NONE;
        }
        return true;
    }
    return false;
}

bool ButtonManager::isPressed(ButtonId id) {
    switch (id) {
        case BTN_POWER:   return _freeinkInput.isPressed(InputManager::BTN_POWER);
        case BTN_CONFIRM: return _freeinkInput.isPressed(InputManager::BTN_CONFIRM);
        case BTN_BACK:    return _freeinkInput.isPressed(InputManager::BTN_BACK) || _freeinkInput.isHomeKeyDown();
        case BTN_LEFT:    return _freeinkInput.isPressed(InputManager::BTN_LEFT);
        case BTN_RIGHT:   return _freeinkInput.isPressed(InputManager::BTN_RIGHT);
        case BTN_UP:      return _freeinkInput.isPressed(InputManager::BTN_UP);
        case BTN_DOWN:    return _freeinkInput.isPressed(InputManager::BTN_DOWN);
        default:          return false;
    }
}

void ButtonManager::clearEvents() {
    _hasPendingEvent = false;
    _heldKey = BTN_NONE;
    _pendingEvent = { BTN_NONE, ACTION_NONE };
}

const char* ButtonManager::getName(ButtonId id) {
    switch (id) {
        case BTN_POWER:   return "POWER";
        case BTN_CONFIRM: return "CONFIRM";
        case BTN_BACK:    return "BACK";
        case BTN_LEFT:    return "LEFT";
        case BTN_RIGHT:   return "RIGHT";
        case BTN_UP:      return "UP";
        case BTN_DOWN:    return "DOWN";
        case BTN_TAP:     return "TAP";
        case BTN_SWIPE_LEFT:  return "SWIPE_LEFT";
        case BTN_SWIPE_RIGHT: return "SWIPE_RIGHT";
        case BTN_SWIPE_UP:    return "SWIPE_UP";
        case BTN_SWIPE_DOWN:  return "SWIPE_DOWN";
        default:          return "NONE";
    }
}
