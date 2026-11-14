#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// Metalio E-Ink 4 (ESP32-S3): 800x480 SSD1677, CST816S touch with capacitive Home key,
// TCA9555 IO expander (UP/DOWN volume buttons), BOOT (GPIO0 confirm), PWR (GPIO3).
inline constexpr DeviceProfile kMetalioEInk4Profile = {
        /*name*/ "METALIO E-INK 4",
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
        /*usbMassStorage*/ true,
        /*touchWake*/ false,
        /*sdPowerPin*/ -1,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 78,
};
