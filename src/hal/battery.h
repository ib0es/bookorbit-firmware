#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>

class BatteryHelper {
public:
    void begin() {}
    float getVoltage() { return (float)_monitor.readVolts(); }
    int getPercentage() { return (int)_monitor.readPercentage(); }
    bool isCharging() { return _monitor.isCharging(); }

private:
    ::BatteryMonitor _monitor;
};

extern BatteryHelper Battery;
