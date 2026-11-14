#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// M5Stack Paper Mono (ESP32-S3): 800x480 SSD1677, FT6336 touch,
// DigitalTwoButton (short UP/DOWN, hold BACK/CONFIRM), PM1 PMIC (POWER), AW9967 frontlight.
inline constexpr DeviceProfile kPaperMonoProfile = {
        /*name*/ "M5STACK PAPER MONO",
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
