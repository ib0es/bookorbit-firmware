// Host test: Wi-Fi failure reasons (src/core/wifi_failure.h, U5).
#include <cstdio>
#include "core/wifi_failure.h"

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main() {
    WifiFailureTracker t;
    CHECK(t.verdict() == WifiFailure::None);
    CHECK(t.atTimeout() == WifiFailure::NoAnswer);

    t.onDisconnect(WifiReason::kAuthFail);                 // explicit: at once
    CHECK(t.verdict() == WifiFailure::BadPassword);

    t.reset();
    t.onDisconnect(WifiReason::kFourWayHandshakeTimeout);  // once may be a fluke
    CHECK(t.verdict() == WifiFailure::None);
    CHECK(t.atTimeout() == WifiFailure::BadPassword);
    t.onDisconnect(WifiReason::kFourWayHandshakeTimeout);
    CHECK(t.verdict() == WifiFailure::BadPassword);

    t.reset();
    t.onDisconnect(WifiReason::kNoApFound);
    CHECK(t.verdict() == WifiFailure::None);
    t.onDisconnect(WifiReason::kNoApRssiThreshold);
    CHECK(t.verdict() == WifiFailure::NoNetwork);

    t.reset();
    t.onDisconnect(8);                                     // leaving: no verdict
    t.onDisconnect(8);
    CHECK(t.verdict() == WifiFailure::None);
    CHECK(t.atTimeout() == WifiFailure::NoAnswer);

    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
