#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// Xteink X4 Classic (ESP32-S3, 8 MB PSRAM): the X4's six navigation keys plus
// POWER, no touch; IMU, RTC, USB mass storage.
inline constexpr DeviceProfile kX4ClassicProfile = {
        /*name*/ "XTEINK X4 CLASSIC",
        /*memory*/ &kLargeMemory,
        /*touch*/ false,
        /*touchOnly*/ false,
        /*upDownTurnPages*/ false,
        /*okOpensBookMenu*/ false,
        /*keyCentres*/ {96, 178, 279, 370},   // as the X4 (not measured on an X4 Classic)
        /*autoRotateByDefault*/ true,
        /*usbMassStorage*/ true,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,                    // SDMMC enable, in the SDK profile
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
