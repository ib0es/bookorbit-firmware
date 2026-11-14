#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// M5Paper v1.1 (classic ESP32, 960x540 IT8951): GT911 touch,
// 3-way rotary wheel: UP (GPIO37), DOWN (GPIO39), press CONFIRM + ext1 POWER wake (GPIO38).
inline constexpr DeviceProfile kM5PaperV11Profile = {
        /*name*/ "M5PAPER V1.1",
        /*memory*/ &kLargeMemory,
        /*touch*/ true,
        /*touchOnly*/ true,
        /*upDownTurnPages*/ true,
        /*okOpensBookMenu*/ true,
        /*hasLeftRight*/ false,
        /*hasUpDown*/ true,
        /*hasPowerButton*/ true,
        /*powerSharedWithBack*/ false,
        /*powerSharedWithConfirm*/ true,
        /*keyCentres*/ {96, 178, 279, 370},
        /*autoRotateByDefault*/ false,
        /*usbMassStorage*/ false,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
