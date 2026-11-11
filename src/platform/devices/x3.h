#pragma once
#include "../device_profile.h"

// Xteink X3 (ESP32-C3, 792x528): the X4's keys, plus an RTC and an IMU.
inline constexpr DeviceProfile kX3Profile = {
    /*name*/ "XTEINK X3",
    /*touch*/ false, /*touchOnly*/ false, /*upDownTurnPages*/ false, /*okOpensBookMenu*/ false,
    /*keyCentres*/ {96, 178, 279, 370},   // as the X4 (not measured on an X3)
    /*autoRotateByDefault*/ true, /*usbMassStorage*/ false, /*touchWake*/ false,
    /*sdPowerPin*/ -1,                    // GPIO13, in the SDK profile
};
