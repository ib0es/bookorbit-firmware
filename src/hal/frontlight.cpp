#include "frontlight.h"

#include <BoardConfig.h>

#if defined(FREEINK_CAP_FRONTLIGHT) && FREEINK_CAP_FRONTLIGHT
#include <FrontlightManager.h>
static FrontlightManager s_manager;
#define FL_REAL 1
#endif

FrontlightHelper Frontlight;

static int clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

bool FrontlightHelper::present() const {
#if defined(FL_REAL)
    return s_manager.present();
#elif defined(UI_FAKE_FRONTLIGHT) && UI_FAKE_FRONTLIGHT
    return true;  // simulator: show the UI
#else
    return false;
#endif
}

bool FrontlightHelper::hasWarmth() const {
#if defined(FL_REAL)
    return s_manager.hasColorTemperature();
#elif defined(UI_FAKE_FRONTLIGHT) && UI_FAKE_FRONTLIGHT
    return true;
#else
    return false;
#endif
}

void FrontlightHelper::begin() {
#if defined(FL_REAL)
    s_manager.begin();
#endif
}

void FrontlightHelper::apply(int brightness, int warm) {
    _brightness = clamp100(brightness);
    _warm = clamp100(warm);
    push();
}

void FrontlightHelper::setEnabled(bool on) {
    if (on == _enabled) return;
    _enabled = on;
    push();
}

void FrontlightHelper::push() {
    const int level = _enabled ? _brightness : 0;
#if defined(FL_REAL)
    if (s_manager.hasColorTemperature()) s_manager.setColorTemperature((uint8_t)_warm);
    s_manager.setBrightness((uint8_t)level);  // 0 = off
#endif
    Serial.printf("[Light] brightness %d%%, warm %d%%%s\n", _brightness, _warm, _enabled ? "" : " (switch off)");
}

void FrontlightHelper::prepareForSleep() {
#if defined(FL_REAL)
    s_manager.prepareForDeepSleep();
#endif
}
