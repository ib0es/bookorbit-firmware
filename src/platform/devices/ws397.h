#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// Waveshare ESP32-S3-ePaper-3.97 (ESP32-S3): 800x480 SSD1677, no touch,
// 4 physical keys: UP (GPIO4), DOWN (GPIO6), CONFIRM/OK (GPIO5), BOOT (GPIO0: click BACK, hold POWER).
inline constexpr DeviceProfile kWs397Profile = {
        /*name*/ "WAVESHARE 3.97",
        /*memory*/ &kLargeMemory,
        /*touch*/ false,
        /*touchOnly*/ false,
        /*upDownTurnPages*/ true,
        /*okOpensBookMenu*/ true,
        /*hasLeftRight*/ false,
        /*hasUpDown*/ true,
        /*hasPowerButton*/ true,
        /*powerSharedWithBack*/ true,
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
