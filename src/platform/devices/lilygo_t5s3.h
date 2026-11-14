#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// LilyGo T5 S3 (ESP32-S3): 960x540 raw parallel display (LgfxEpd), GT911 touch,
// single physical button: BOOT/POWER (GPIO0), PWM frontlight.
inline constexpr DeviceProfile kLilyGoProfile = {
        /*name*/ "LILYGO T5 S3",
        /*memory*/ &kLargeMemory,
        /*touch*/ true,
        /*touchOnly*/ true,
        /*upDownTurnPages*/ true,
        /*okOpensBookMenu*/ true,
        /*hasLeftRight*/ false,
        /*hasUpDown*/ false,
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
