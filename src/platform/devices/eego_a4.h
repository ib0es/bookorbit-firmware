#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// EEGO Reader A4 (ESP32-S3): 768x552 UC8279C panel, GSLX680 touch with Home key,
// 3 physical keys: UP (GPIO5), DOWN (GPIO7), active-HIGH POWER (GPIO8).
inline constexpr DeviceProfile kEegoA4Profile = {
        /*name*/ "EEGO A4",
        /*memory*/ &kLargeMemory,
        /*touch*/ true,
        /*touchOnly*/ true,
        /*upDownTurnPages*/ true,
        /*okOpensBookMenu*/ true,
        /*hasLeftRight*/ false,
        /*hasUpDown*/ true,
        /*hasPowerButton*/ true,
        /*powerSharedWithBack*/ false,
        /*powerSharedWithConfirm*/ false,
        /*keyCentres*/ {96, 178, 279, 370},
        /*autoRotateByDefault*/ false,
        /*usbMassStorage*/ false,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
