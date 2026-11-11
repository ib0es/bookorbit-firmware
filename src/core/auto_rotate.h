#pragma once

// Pure gravity-to-orientation filter for IMU auto-rotate (host-tested).
// Orientation codes match Book Orbit's uiOrientation convention:
//   0 = Portrait, 1 = Landscape (90 deg CCW from portrait),
//   2 = Portrait upside down, 3 = Landscape upside down.

#include <cstdint>

struct ImuCalibration {
    int rot = 0;       // 0..3 quarter-turns applied after axis flips
    bool flipX = false;
    bool flipY = false;
};

class AutoRotateTracker {
public:
    static constexpr uint32_t kHoldMs = 700;
    static constexpr uint32_t kCooldownMs = 1500;
    static constexpr float kFlatMaxZ = 0.80f;
    static constexpr float kMinAxisG = 0.55f;
    static constexpr float kAxisRatio = 1.6f;

    // Maps calibrated gravity vector to orientation 0..3, or -1 if flat / diagonal / unstable.
    static int classifyGravity(float ax, float ay, float az, const ImuCalibration& cal);

    void reset();

    // Feed a new accelerometer sample at `nowMs`. Returns new orientation 0..3
    // when a tilt is sustained past holdMs and outside cooldown, else -1.
    int update(float ax, float ay, float az, int currentUiOrient, uint32_t nowMs, const ImuCalibration& cal);

private:
    bool _hasPending = false;
    int _pendingOrient = 0;
    uint32_t _pendingSinceMs = 0;
    bool _hasSwitched = false;
    uint32_t _lastSwitchMs = 0;
};
