#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// de-link (ESP32-S3): seven physical buttons (BACK, OK, LEFT, RIGHT, UP, DOWN, POWER),
// single-channel PWM frontlight, no touch.
inline constexpr DeviceProfile kDeLinkProfile = {
        /*name*/ "DE-LINK",
        /*memory*/ &kLargeMemory,
        /*touch*/ false,
        /*touchOnly*/ false,
        /*upDownTurnPages*/ false,
        /*okOpensBookMenu*/ false,
        /*hasLeftRight*/ true,
        /*hasUpDown*/ true,
        /*hasPowerButton*/ true,
        /*powerSharedWithBack*/ false,
        /*powerSharedWithConfirm*/ false,
        /*keyCentres*/ {86, 186, 294, 394},
        /*autoRotateByDefault*/ false,
        /*usbMassStorage*/ true,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 96,  // the keys are 100+ px apart: room for the longer labels
};
