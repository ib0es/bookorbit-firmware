#pragma once
#include "../device_profile.h"

// Xteink X4 (ESP32-C3): seven keys (BACK, OK, LEFT, RIGHT under the screen,
// UP / DOWN on the side, POWER), no touch, no frontlight.
inline constexpr DeviceProfile kX4Profile = {
    /*name*/ "XTEINK X4",
    /*touch*/ false, /*touchOnly*/ false, /*upDownTurnPages*/ false, /*okOpensBookMenu*/ false,
    /*keyCentres*/ {96, 178, 279, 370},   // measured on the X4: two rockers of two keys
    /*autoRotateByDefault*/ false, /*usbMassStorage*/ false, /*touchWake*/ false,
    // The SDK's X4 profile leaves the SD rail unassigned although the board has
    // the same GPIO13 switch as the X3 (active-high).
    /*sdPowerPin*/ 13,
};
