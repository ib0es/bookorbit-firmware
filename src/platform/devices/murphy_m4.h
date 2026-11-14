#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// Murphy M4 (ESP32-S3): 800x480 SSD1677, FT6336U touch,
// DigitalFiveKey: UP (GPIO1), DOWN (GPIO2), confirm + power shared (GPIO0), dual PWM frontlight.
inline constexpr DeviceProfile kMurphyM4Profile = {
        /*name*/ "MURPHY M4",
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
        /*usbMassStorage*/ true,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
