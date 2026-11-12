#include "input.h"

#include "app_state.h"
#include "../platform/device.h"
#include "../reader/reader.h"
#include "../ui/kit/ui_kit.h"

namespace Input {
namespace {
AppContext* g_ctx = nullptr;

bool keysUpsideDown() {
    if (!g_ctx || uikit::touchOnly()) return false;
    const int o = g_ctx->currentState == STATE_READING ? Reader.orientation() : g_ctx->config.uiOrientation;
    return (o & 3) == 2;
}
}  // namespace

ButtonId orient(ButtonId physical) {
    if ((physical == BTN_LEFT || physical == BTN_RIGHT) && keysUpsideDown()) {
        return physical == BTN_LEFT ? BTN_RIGHT : BTN_LEFT;
    }
    return physical;
}

void install(AppContext& ctx) {
    g_ctx = &ctx;
    Buttons.setTouchMapper(uikit::touchToLogical);
    Buttons.setKeyRemap(orient);
    // C4: a touch-only board with a silent touch panel would have no OK and no
    // BACK: a held DOWN is OK, a held UP is BACK, and the footer says so.
    const bool noTouch = Platform::kDevice.touchOnly && !Buttons.hasTouch();
    if (noTouch) Serial.println("[Input] No touch panel: held DOWN = OK, held UP = BACK");
    Buttons.setHoldFallback(noTouch);
    uikit::setTouchMissing(noTouch);
}

}  // namespace Input
