#pragma once

// Hardware IMU wrapper (QMI8658 / LSM6DS3 / BMI270 / SC7A20H via freeink::Imu).
// Manages sleep/wake power state so disabling auto-rotate puts the chip into
// microamp standby rather than leaving it sampling at ~1 mA.

#include <Arduino.h>
#include <Imu.h>

class ImuHelper {
public:
    bool begin();
    bool present() const { return _imu.present(); }
    bool active() const { return _imu.present() && _active; }

    // Enables or suspends hardware sampling to match the auto-rotate setting.
    void setActive(bool enabled);
    bool sleep();

    bool readAccel(float& ax, float& ay, float& az);
    bool hasLastSample() const { return _hasLast; }
    float lastAx() const { return _lastAx; }
    float lastAy() const { return _lastAy; }
    float lastAz() const { return _lastAz; }

private:
    freeink::Imu _imu;
    bool _active = false;
    bool _hasLast = false;
    float _lastAx = 0.0f;
    float _lastAy = 0.0f;
    float _lastAz = 0.0f;
};

extern ImuHelper BoardImu;
