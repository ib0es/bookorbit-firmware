#pragma once

class BatteryMonitor {
public:
    float readVolts() { return 3.92f; }
    int readPercentage() { return 82; }
    bool isCharging() { return false; }
};
