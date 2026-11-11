// Host unit tests for X4 Pro helpers (FrontlightPolicy, LightSleepPolicy). Run: test/host/run.sh
#include "hal/frontlight_policy.h"
#include "services/light_sleep_policy.h"

#include <cstdio>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static LightSleepPolicy::Inputs safeInputs() {
    LightSleepPolicy::Inputs in;
    in.featureBuilt = true;
    in.settingEnabled = true;
    in.touchWakeIrqPin = 10;
    in.wifiOff = true;
    in.syncBgActive = false;
    in.readerBgActive = false;
    in.usbMscActive = false;
    in.debouncePending = false;
    in.touchPressed = false;
    in.homeKeyDown = false;
    in.usbSerialConnected = false;
    in.irqAlreadyAsserted = false;
    in.idleMs = 600;
    return in;
}

static void testFrontlightPolicy() {
    int remembered = 50;
    CHECK(FrontlightPolicy::toggleLevel(0, remembered) == 50);
    CHECK(remembered == 50);

    CHECK(FrontlightPolicy::toggleLevel(75, remembered) == 0);
    CHECK(remembered == 75);
    CHECK(FrontlightPolicy::toggleLevel(0, remembered) == 75);

    // Clamp > 100 and fall back when invalid remembered value is supplied.
    CHECK(FrontlightPolicy::toggleLevel(130, remembered) == 0);
    CHECK(remembered == 100);

    remembered = 0;
    CHECK(FrontlightPolicy::toggleLevel(0, remembered, 40) == 40);
    remembered = 150;
    CHECK(FrontlightPolicy::toggleLevel(0, remembered, 50) == 50);
}

static void testLightSleepPolicyEligibility() {
    CHECK(LightSleepPolicy::canEnter(safeInputs()));

    { auto in = safeInputs(); in.featureBuilt = false; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.settingEnabled = false; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.touchWakeIrqPin = -1; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.wifiOff = false; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.syncBgActive = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.readerBgActive = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.usbMscActive = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.debouncePending = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.touchPressed = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.homeKeyDown = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.usbSerialConnected = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.irqAlreadyAsserted = true; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.idleMs = 499; CHECK(!LightSleepPolicy::canEnter(in)); }
    { auto in = safeInputs(); in.idleMs = 500; CHECK(LightSleepPolicy::canEnter(in)); }
}

static void testLightSleepTiming() {
    LightSleepPolicy::WakeTiming base;
    base.onBattery = true;
    base.idleMs = 1000;
    base.sleepTimeoutMs = 300000;
    base.batteryCheckIntervalMs = 60000;
    base.msSinceBatteryCheck = 1000;
    base.maxSleepMs = 15000;
    CHECK(LightSleepPolicy::nextWakeDelayMs(base) == 15000);

    // Battery deep-sleep timeout dominates near the end of idle period on battery.
    {
        auto t = base;
        t.idleMs = 297500;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 2500);
        t.idleMs = 300500;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 0);
        // While charging, deep-sleep idle timeout does not truncate wake delay.
        t.onBattery = false;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 15000);
    }

    // Battery periodic check deadline.
    {
        auto t = base;
        t.msSinceBatteryCheck = 58200;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 1800);
        t.msSinceBatteryCheck = 61000;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 0);
    }

    // Timed modal auto-close deadline.
    {
        auto t = base;
        t.modalRemainingMs = 950;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 950);
    }

    // Reader chapter prelayout deadline only applies while waiting.
    {
        auto t = base;
        t.waitingPrelayout = false;
        t.msToPrelayout = 800;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 15000);
        t.waitingPrelayout = true;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 800);
    }

    // Reader status-bar clock minute boundary only applies when active.
    {
        auto t = base;
        t.readingClockActive = false;
        t.msToNextMinute = 4200;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 15000);
        t.readingClockActive = true;
        CHECK(LightSleepPolicy::nextWakeDelayMs(t) == 4200);
    }
}

int main() {
    testFrontlightPolicy();
    testLightSleepPolicyEligibility();
    testLightSleepTiming();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
