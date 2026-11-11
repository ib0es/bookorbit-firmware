#include "imu.h"

ImuHelper BoardImu;

bool ImuHelper::begin() {
    if (!_imu.begin()) {
        _active = false;
        _hasLast = false;
        Serial.println("[IMU] Not detected or unsupported on this board");
        return false;
    }
    _active = true;
    freeink::Imu::Sample s{};
    if (_imu.read(s)) {
        _hasLast = true;
        _lastAx = s.ax;
        _lastAy = s.ay;
        _lastAz = s.az;
        Serial.printf("[IMU] Ready (ax=%.2f ay=%.2f az=%.2f)\n", s.ax, s.ay, s.az);
    } else {
        Serial.println("[IMU] Ready (waiting for first sample)");
    }
    return true;
}

void ImuHelper::setActive(bool enabled) {
    if (!_imu.present()) return;
    if (enabled == _active) return;
    if (enabled) {
        if (_imu.wake()) {
            _active = true;
            Serial.println("[IMU] Woke for auto-rotate");
        }
    } else {
        sleep();
    }
}

bool ImuHelper::sleep() {
    if (!_imu.present()) return false;
    const bool ok = _imu.sleep();
    _active = false;
    Serial.println("[IMU] Put to sleep");
    return ok;
}

bool ImuHelper::readAccel(float& ax, float& ay, float& az) {
    if (!_imu.present() || !_active) return false;
    freeink::Imu::Sample s{};
    if (!_imu.read(s)) return false;
    ax = _lastAx = s.ax;
    ay = _lastAy = s.ay;
    az = _lastAz = s.az;
    _hasLast = true;
    return true;
}
