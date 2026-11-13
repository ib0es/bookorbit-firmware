// Host unit tests for X4 Classic helpers & configuration defaults
// (PowerPolicy idle deep sleep guard + X4C AppConfig/settings layout).
// Run: test/host/run.sh
#include "app/app_state.h"
#include "platform/hal/frontlight.h"
#include "platform/hal/storage.h"
#include "storage/config_store.h"
#include "core/power_policy.h"

#include <cstdio>
#include <cstring>

// Minimal link stubs required by src/app/app_state.cpp in standalone host test.
FrontlightHelper Frontlight;
bool FrontlightHelper::present() const { return false; }

StorageManager::StorageManager() {}
StorageManager Storage;
bool ConfigStore::load(AppConfig&) { return false; }
bool ConfigStore::save(const AppConfig&) { return false; }

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                  \
    do {                                                             \
        g_checks++;                                                  \
        if (!(cond)) {                                               \
            g_failed++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                            \
    } while (0)

static void testPowerPolicyIdleDeepSleep() {
    constexpr uint32_t kTimeout = 300000;

    // Below timeout: never sleep even on pure battery.
    CHECK(!PowerPolicy::shouldEnterIdleDeepSleep(false, false, false, 299999, kTimeout));
    // Reached timeout on battery without USB MSC or Serial -> sleep.
    CHECK(!PowerPolicy::shouldBlockIdleDeepSleep(false, false, false));
    CHECK(PowerPolicy::shouldEnterIdleDeepSleep(false, false, false, 300000, kTimeout));
    CHECK(PowerPolicy::shouldEnterIdleDeepSleep(false, false, false, 400000, kTimeout));

    // Active USB-MSC blocks idle sleep.
    CHECK(PowerPolicy::shouldBlockIdleDeepSleep(true, false, false));
    CHECK(!PowerPolicy::shouldEnterIdleDeepSleep(true, false, false, 500000, kTimeout));

    // Hardware charging (STAT HIGH) blocks idle sleep.
    CHECK(PowerPolicy::shouldBlockIdleDeepSleep(false, true, false));
    CHECK(!PowerPolicy::shouldEnterIdleDeepSleep(false, true, false, 500000, kTimeout));

    // 100% charged on USB cable (GPIO21 STAT LOW -> charging==false), while CDC
    // Serial is connected -> must NOT enter deep sleep.
    CHECK(PowerPolicy::shouldBlockIdleDeepSleep(false, false, true));
    CHECK(!PowerPolicy::shouldEnterIdleDeepSleep(false, false, true, 500000, kTimeout));
}

static void testX4ClassicDefaultsAndSettingsLayout() {
    AppConfig cfg;
    // The X4 Classic profile (src/platform/devices/x4c.h).
    CHECK(strcmp(Platform::kDevice.name, "XTEINK X4 CLASSIC") == 0);
    CHECK(!Platform::kDevice.touch && !Platform::kDevice.touchOnly);
    CHECK(!Platform::kDevice.upDownTurnPages && !Platform::kDevice.okOpensBookMenu);
    CHECK(cfg.autoRotate == true);
    CHECK(cfg.usbMscEnabled == true);

    CHECK(hasUsbMscSetting());
    CHECK(!hasTouchWakeSetting());
    CHECK(readingSettingIndex() == 7);
    CHECK(orientationSettingIndex() == 8);
    CHECK(usbMscSettingIndex() == 9);
    CHECK(touchWakeSettingIndex() == -1);
    CHECK(webSettingIndex() == 10);
    CHECK(settingsItemCount() == 11);
}

static void testPowerPolicyLeaveUsbMsc() {
    constexpr uint32_t kTimeout = 300000;
    CHECK(!PowerPolicy::shouldLeaveUsbMsc(true, kTimeout * 10, kTimeout));   // a computer: stays
    CHECK(!PowerPolicy::shouldLeaveUsbMsc(false, kTimeout - 1, kTimeout));   // not yet
    CHECK(PowerPolicy::shouldLeaveUsbMsc(false, kTimeout, kTimeout));        // no computer for the timeout
}

int main() {
    testPowerPolicyIdleDeepSleep();
    testPowerPolicyLeaveUsbMsc();
    testX4ClassicDefaultsAndSettingsLayout();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
