#include "auto_rotate.h"

#include <cmath>

int AutoRotateTracker::classifyGravity(float ax, float ay, float az, const ImuCalibration& cal) {
    float x = cal.flipX ? -ax : ax;
    float y = cal.flipY ? -ay : ay;

    float rx = x, ry = y;
    switch (cal.rot & 3) {
        case 1:
            rx = -y;
            ry = x;
            break;
        case 2:
            rx = -x;
            ry = -y;
            break;
        case 3:
            rx = y;
            ry = -x;
            break;
        default:
            break;
    }

    const float magSq = rx * rx + ry * ry + az * az;
    if (magSq < 0.25f || magSq > 4.0f) return -1;
    if (std::fabs(az) > kFlatMaxZ) return -1;

    const float absX = std::fabs(rx);
    const float absY = std::fabs(ry);
    if (absY >= kMinAxisG && absY >= absX * kAxisRatio) {
        return ry < 0.0f ? 0 : 2;
    }
    if (absX >= kMinAxisG && absX >= absY * kAxisRatio) {
        return rx < 0.0f ? 1 : 3;
    }
    return -1;
}

void AutoRotateTracker::reset() { _hasPending = false; }

int AutoRotateTracker::update(float ax, float ay, float az, int currentUiOrient, uint32_t nowMs,
                              const ImuCalibration& cal) {
    if (_hasSwitched && (uint32_t)(nowMs - _lastSwitchMs) < kCooldownMs) {
        _hasPending = false;
        return -1;
    }

    const int cand = classifyGravity(ax, ay, az, cal);
    if (cand < 0 || cand == (currentUiOrient & 3)) {
        _hasPending = false;
        return -1;
    }

    if (!_hasPending || cand != _pendingOrient) {
        _hasPending = true;
        _pendingOrient = cand;
        _pendingSinceMs = nowMs;
        return -1;
    }

    if ((uint32_t)(nowMs - _pendingSinceMs) < kHoldMs) {
        return -1;
    }

    _hasPending = false;
    _hasSwitched = true;
    _lastSwitchMs = nowMs;
    return cand;
}
