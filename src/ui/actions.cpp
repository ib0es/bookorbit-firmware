#include "actions.h"

#include "../platform/device.h"

namespace Actions {

Action of(const ButtonEvent& e) {
    if (e.action == ACTION_LONG_PRESS) return e.id == BTN_BACK ? Action::Menu : Action::None;
    if (e.action != ACTION_CLICK) return Action::None;
    switch (e.id) {
        case BTN_UP:      return Action::Up;
        case BTN_DOWN:    return Action::Down;
        case BTN_CONFIRM: return Action::Select;
        case BTN_BACK:    return Action::Back;
        case BTN_LEFT:    return Action::Prev;
        case BTN_RIGHT:   return Action::Next;
        default:          return Action::None;
    }
}

Action reading(const ButtonEvent& e, bool upDownTurnPagesSetting) {
    const Action a = of(e);
    if (Platform::kDevice.upDownTurnPages || upDownTurnPagesSetting) {
        if (a == Action::Down) return Action::Next;
        if (a == Action::Up) return Action::Prev;
    }
    return a;
}

bool okOpensRowMenu() { return Platform::kDevice.okOpensBookMenu; }

}  // namespace Actions
