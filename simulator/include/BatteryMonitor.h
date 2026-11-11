#pragma once

#include <cstdlib>

// Simulator battery: 82 %, 3.92 V, on battery. The headless tests set
// SIM_BATTERY_PCT / SIM_BATTERY_VOLTS / SIM_USB=1 to try the power rules.
class BatteryMonitor {
public:
    float readVolts() { const char* v = getenv("SIM_BATTERY_VOLTS"); return v ? (float)atof(v) : 3.92f; }
    int readPercentage() { const char* v = getenv("SIM_BATTERY_PCT"); return v ? atoi(v) : 82; }
    bool isCharging() { return false; }
    bool isExternalPowerPresent(bool* known = nullptr) {
        if (known) *known = true;
        const char* v = getenv("SIM_USB");
        return v && *v == '1';
    }
    static bool loadDesignCapacity() { return false; }
};
