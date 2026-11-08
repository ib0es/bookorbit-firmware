#include "buttons.h"
#include "../ui/kit/ui_kit.h"

ButtonManager Buttons;

ButtonManager::ButtonManager() : _hasPendingEvent(false), _powerLongPressFired(false) {
    _pendingEvent = { BTN_NONE, ACTION_NONE };
}

void ButtonManager::begin() {
    _freeinkInput.begin();
    Serial.printf("[Buttons] FreeInk InputManager initialized, touch: %s\n",
                  _freeinkInput.hasTouch() ? "on" : "off");
    if (_freeinkInput.hasToggleSwitch())
        Serial.printf("[Buttons] Light switch at boot: %s\n", _freeinkInput.isToggleSwitchOn() ? "on" : "off");
}

void ButtonManager::update() {
    _freeinkInput.update();

    // The switch edge is one-shot per update(): latch it before the early return.
    if (_freeinkInput.wasToggleSwitchChanged()) {
        _lightSwitchChanged = true;
        Serial.printf("[Buttons] Light switch: %s\n", _freeinkInput.isToggleSwitchOn() ? "on" : "off");
    }

    if (_hasPendingEvent) return; // Keep pending until consumed

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
        return true;
    }
    return false;
}

bool ButtonManager::isPressed(ButtonId id) {
    switch (id) {
        case BTN_POWER:   return _freeinkInput.isPressed(InputManager::BTN_POWER);
        case BTN_CONFIRM: return _freeinkInput.isPressed(InputManager::BTN_CONFIRM);
        case BTN_BACK:    return _freeinkInput.isPressed(InputManager::BTN_BACK);
        case BTN_LEFT:    return _freeinkInput.isPressed(InputManager::BTN_LEFT);
        case BTN_RIGHT:   return _freeinkInput.isPressed(InputManager::BTN_RIGHT);
        case BTN_UP:      return _freeinkInput.isPressed(InputManager::BTN_UP);
        case BTN_DOWN:    return _freeinkInput.isPressed(InputManager::BTN_DOWN);
        default:          return false;
    }
}

void ButtonManager::clearEvents() {
    _hasPendingEvent = false;
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
