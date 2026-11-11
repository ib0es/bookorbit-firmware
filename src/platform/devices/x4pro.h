#pragma once
#include "../device_profile.h"

// Xteink X4 Pro (ESP32-S3, 8 MB PSRAM): GT911 touch with a capacitive Home key
// (tap = BACK, hold = reader menu), two side keys (UP / DOWN) and POWER;
// warm/cool frontlight, RTC, USB mass storage, touch wake.
inline constexpr DeviceProfile kX4ProProfile = {
    /*name*/ "XTEINK X4 PRO",
    /*touch*/ true, /*touchOnly*/ true, /*upDownTurnPages*/ true, /*okOpensBookMenu*/ true,
    /*keyCentres*/ {96, 178, 279, 370},   // unused: no key hint tabs on a touch-only board
    /*autoRotateByDefault*/ false, /*usbMassStorage*/ true, /*touchWake*/ true,
    /*sdPowerPin*/ -1,                    // SDMMC enable, in the SDK profile
};
