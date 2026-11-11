#pragma once
#include "../device_profile.h"

// BOOX Picco (ESP32-S3, 8 MB PSRAM): TMA525C touch, UP / DOWN / POWER keys, a
// frontlight slide switch; warm/cool frontlight, RTC, IMU.
inline constexpr DeviceProfile kPiccoProfile = {
    /*name*/ "BOOX PICCO",
    /*touch*/ true, /*touchOnly*/ true, /*upDownTurnPages*/ true, /*okOpensBookMenu*/ true,
    /*keyCentres*/ {96, 178, 279, 370},   // unused: no key hint tabs on a touch-only board
    /*autoRotateByDefault*/ false, /*usbMassStorage*/ false, /*touchWake*/ false,
    /*sdPowerPin*/ -1,                    // GPIO0 rail, in the SDK profile
};
