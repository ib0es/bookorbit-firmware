#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>

class BatteryHelper {
public:
    void begin() {}
    float getVoltage() { return (float)_monitor.readVolts(); }
    int getPercentage() { return (int)_monitor.readPercentage(); }
    // Plugged in: charging, or a cable detected. The X4 has no charge-status pin
    // (BatteryMonitor::isCharging() is always false there) but sees USB on GPIO20,
    // so this is what keeps it awake on USB and shows the "+" by the battery.
    bool isCharging() {
        if (_monitor.isCharging()) return true;
        bool known = false;
        const bool present = _monitor.isExternalPowerPresent(&known);
        return known && present;
    }

private:
    ::BatteryMonitor _monitor;
};

extern BatteryHelper Battery;
