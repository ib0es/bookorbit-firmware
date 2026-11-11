#pragma once

// Simulator IMU stub matching freeink::Imu.
// Headless tests can enable it with SIM_IMU_PRESENT=1 and inject gravity via
// SIM_IMU_ACCEL="ax,ay,az" or Imu::simSetAccel(...). Default sample is upright
// portrait (0, -1, 0).

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace freeink {

class Imu {
public:
    struct Sample {
        float ax, ay, az;
        float gx, gy, gz;
    };

    static void simSetAccel(float ax, float ay, float az) {
        s_overrideActive() = true;
        s_ax() = ax;
        s_ay() = ay;
        s_az() = az;
    }

    static void simClearAccel() { s_overrideActive() = false; }

    bool begin() {
        const char* pres = std::getenv("SIM_IMU_PRESENT");
        const char* acc = std::getenv("SIM_IMU_ACCEL");
        begun_ = (pres && *pres == '1') || (acc && *acc);
        sleeping_ = false;
        return begun_;
    }

    bool present() const { return begun_; }
    bool hasGyroscope() const { return begun_; }

    bool read(Sample& out) {
        if (!begun_ || sleeping_) return false;
        float ax = 0.0f, ay = -1.0f, az = 0.0f;
        if (s_overrideActive()) {
            ax = s_ax();
            ay = s_ay();
            az = s_az();
        } else if (const char* acc = std::getenv("SIM_IMU_ACCEL")) {
            std::sscanf(acc, "%f,%f,%f", &ax, &ay, &az);
        }
        out = Sample{ax, ay, az, 0.0f, 0.0f, 0.0f};
        return true;
    }

    bool sleep() {
        if (!begun_) return false;
        sleeping_ = true;
        return true;
    }

    bool wake() {
        if (!begun_) return false;
        sleeping_ = false;
        return true;
    }

private:
    static bool& s_overrideActive() {
        static bool v = false;
        return v;
    }
    static float& s_ax() {
        static float v = 0.0f;
        return v;
    }
    static float& s_ay() {
        static float v = -1.0f;
        return v;
    }
    static float& s_az() {
        static float v = 0.0f;
        return v;
    }

    bool begun_ = false;
    bool sleeping_ = false;
};

}  // namespace freeink

using Imu = freeink::Imu;
