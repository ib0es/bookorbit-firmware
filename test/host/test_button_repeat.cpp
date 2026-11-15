#include <cassert>
#include <chrono>
#include <thread>
#include "../../src/platform/hal/buttons.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = {};
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = {};
void simPumpEvents() {}
void simInputPolled() {}

int main() {
    ButtonManager buttons;
    ButtonEvent event;
    InputManager::simPress(InputManager::BTN_DOWN);
    buttons.update();
    assert(buttons.getEvent(event) && event.id == BTN_DOWN && event.repeat == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(510));
    buttons.update(); // A repeat queued while the panel is busy.
    InputManager::simRelease(InputManager::BTN_DOWN);
    buttons.update();
    assert(!buttons.getEvent(event)); // Released repeats must not move the next screen.
    InputManager::simPress(InputManager::BTN_UP);
    buttons.update();
    assert(buttons.getEvent(event) && event.id == BTN_UP && event.repeat == 0);
    assert(!buttons.getEvent(event));
}
