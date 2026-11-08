#pragma once

namespace freeink {
class PowerManager {
public:
    static void waitForPowerButtonRelease() {}
    static void armPowerButtonWakeup() {}
    static void powerDownRailsForSleep() {}
    static void deepSleep() {}
    static void deepSleepUntilPowerButton() {}
};
}
