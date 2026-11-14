#pragma once
#include "../device_profile.h"
#include "../tiers/small.h"

// OnePage (ESP32-C61, 2 MB PSRAM): 800x480 SSD1677, no touch,
// 4 front keys on GPIO4 ADC ladder (BACK, OK, LEFT, RIGHT) + 3 side GPIO keys (UP, DOWN, POWER).
inline constexpr DeviceProfile kOnePageProfile = {
        /*name*/ "ONEPAGE",
        /*memory*/ &kSmallMemory,
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
        /*usbMassStorage*/ false,
        /*touchWake*/ false,
        /*sdPowerPin*/ 27,
        /*imuMountRot*/ 0,
        /*imuMountFlipY*/ false,
        /*keyHintWidth*/ 85,
};
