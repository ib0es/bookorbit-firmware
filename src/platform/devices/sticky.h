#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// Seeed reTerminal Sticky (ESP32-S3): 800x480 SSD1677, GT911 touch,
// DigitalConfirmPowerHold (GPIO4: click CONFIRM, hold POWER), UP (GPIO5), DOWN (GPIO6).
inline constexpr DeviceProfile kStickyProfile = {
        /*name*/ "RETERMINAL STICKY",
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
        /*autoRotateByDefault*/ true,
        /*usbMassStorage*/ false,
        /*touchWake*/ true,
        /*sdPowerPin*/ 10,
        /*imuMountRot*/ 1,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
