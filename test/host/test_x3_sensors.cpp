// Host unit tests for pure time/calendar formatting and IMU auto-rotate filter.
// Run: test/host/run.sh
#include "services/time_utils.h"
#include "app/auto_rotate.h"

#include <cstdio>
#include <cstring>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testCivilRoundTrip() {
    struct Case { int y, m, d, hh, mm, ss, wd; };
    const Case cases[] = {
        {1970, 1, 1, 0, 0, 0, 4},       // Thu
        {2000, 2, 29, 12, 34, 56, 2},   // leap century Tue
        {2024, 2, 29, 23, 59, 59, 4},   // leap day Thu
        {2025, 12, 31, 23, 59, 59, 3},  // NYE Wed
        {2026, 11, 11, 10, 5, 42, 3},   // Wed
        {2100, 2, 28, 18, 0, 0, 0},     // non-leap century Sun
    };
    for (const auto& c : cases) {
        const int64_t utc = TimeUtils::utcFromCivil(c.y, c.m, c.d, c.hh, c.mm, c.ss);
        const TimeUtils::CivilDateTime back = TimeUtils::civilFromUtc(utc);
        CHECK(back.year == c.y && back.month == c.m && back.day == c.d);
        CHECK(back.hour == c.hh && back.minute == c.mm && back.second == c.ss);
        CHECK(back.weekday == c.wd);
    }
}

static void testClockAndTimezone() {
    char buf[32];
    const int64_t nye = TimeUtils::utcFromCivil(2026, 1, 1, 1, 15, 0);
    // Negative offset wraps across midnight/year boundary cleanly: 2026-01-01 01:15 UTC-3 -> 22:15 (2025-12-31)
    CHECK(TimeUtils::formatClockHHMM(nye, -180, buf, sizeof(buf)) && strcmp(buf, "22:15") == 0);
    // Half-hour offset: +5:30 -> 06:45
    CHECK(TimeUtils::formatClockHHMM(nye, 330, buf, sizeof(buf)) && strcmp(buf, "06:45") == 0);
    // Invalid epoch (0): returns false and clears string
    buf[0] = 'x';
    CHECK(!TimeUtils::formatClockHHMM(0, 180, buf, sizeof(buf)) && buf[0] == '\0');

    TimeUtils::formatUtcOffset(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "UTC+0") == 0);
    TimeUtils::formatUtcOffset(180, buf, sizeof(buf));
    CHECK(strcmp(buf, "UTC+3") == 0);
    TimeUtils::formatUtcOffset(-300, buf, sizeof(buf));
    CHECK(strcmp(buf, "UTC-5") == 0);
    TimeUtils::formatUtcOffset(330, buf, sizeof(buf));
    CHECK(strcmp(buf, "UTC+5:30") == 0);
    TimeUtils::formatUtcOffset(-210, buf, sizeof(buf));
    CHECK(strcmp(buf, "UTC-3:30") == 0);

    CHECK(TimeUtils::clampUtcOffset(-900) == -720);
    CHECK(TimeUtils::clampUtcOffset(999) == 840);
    CHECK(TimeUtils::stepUtcOffset(0, 1) == 30);
    CHECK(TimeUtils::stepUtcOffset(0, -1) == -30);
    CHECK(TimeUtils::stepUtcOffset(840, 1) == -720);
    CHECK(TimeUtils::stepUtcOffset(-720, -1) == 840);
}

static void testAutoRotate() {
    const ImuCalibration id{};
    // Gravity axis mapping without calibration
    CHECK(AutoRotateTracker::classifyGravity(0.0f, -1.0f, 0.0f, id) == 0);
    CHECK(AutoRotateTracker::classifyGravity(-1.0f, 0.0f, 0.0f, id) == 1);
    CHECK(AutoRotateTracker::classifyGravity(0.0f, 1.0f, 0.0f, id) == 2);
    CHECK(AutoRotateTracker::classifyGravity(1.0f, 0.0f, 0.0f, id) == 3);
    // Flat on table & 45-degree diagonal & shake/freefall rejected
    CHECK(AutoRotateTracker::classifyGravity(0.0f, -0.3f, 0.92f, id) == -1);
    CHECK(AutoRotateTracker::classifyGravity(0.707f, -0.707f, 0.0f, id) == -1);
    CHECK(AutoRotateTracker::classifyGravity(0.1f, -0.1f, 0.1f, id) == -1);
    CHECK(AutoRotateTracker::classifyGravity(0.0f, -3.0f, 0.0f, id) == -1);

    // Calibration turns/flips: flipY turns (0, +1, 0) into portrait 0
    CHECK(AutoRotateTracker::classifyGravity(0.0f, 1.0f, 0.0f, ImuCalibration{0, false, true}) == 0);
    // rot=1 turns (-1, 0, 0) -> (0, -1) -> portrait 0
    CHECK(AutoRotateTracker::classifyGravity(-1.0f, 0.0f, 0.0f, ImuCalibration{1, false, false}) == 0);

    // Hold debounce + cooldown
    AutoRotateTracker tr;
    CHECK(tr.update(-1.0f, 0.0f, 0.0f, 0, 100, id) == -1);
    CHECK(tr.update(-1.0f, 0.0f, 0.0f, 0, 600, id) == -1);   // 500ms < 700ms
    CHECK(tr.update(-1.0f, 0.0f, 0.0f, 0, 800, id) == 1);    // 700ms -> switch!
    // During 1500ms cooldown (until 2300ms), flipping back is ignored
    CHECK(tr.update(0.0f, -1.0f, 0.0f, 1, 1500, id) == -1);
    CHECK(tr.update(0.0f, -1.0f, 0.0f, 1, 2250, id) == -1);
    // After cooldown expires: requires a fresh 700ms hold
    CHECK(tr.update(0.0f, -1.0f, 0.0f, 1, 2400, id) == -1);
    CHECK(tr.update(0.0f, -1.0f, 0.0f, 1, 3099, id) == -1);
    CHECK(tr.update(0.0f, -1.0f, 0.0f, 1, 3100, id) == 0);

    // Momentary flat transition resets hold timer
    tr.reset();
    CHECK(tr.update(1.0f, 0.0f, 0.0f, 0, 5000, id) == -1);
    CHECK(tr.update(0.0f, 0.0f, 1.0f, 0, 5400, id) == -1);   // flat -> resets pending
    CHECK(tr.update(1.0f, 0.0f, 0.0f, 0, 5600, id) == -1);   // restarted at 5600
    CHECK(tr.update(1.0f, 0.0f, 0.0f, 0, 6299, id) == -1);
    CHECK(tr.update(1.0f, 0.0f, 0.0f, 0, 6300, id) == 3);
}

int main() {
    testCivilRoundTrip();
    testClockAndTimezone();
    testAutoRotate();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
