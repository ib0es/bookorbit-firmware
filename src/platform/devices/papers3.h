#pragma once
#include "../device_profile.h"
#include "../tiers/large.h"

// M5Stack PaperS3 (ESP32-S3, 960x540 raw parallel display via LgfxEpd): GT911 touch,
// ZERO physical buttons (PMS150G manages power, software power off via BoardPaperS3::powerOff).
inline constexpr DeviceProfile kPaperS3Profile = {
        /*name*/ "M5STACK PAPERS3",
        /*memory*/ &kLargeMemory,
        /*touch*/ true,
        /*touchOnly*/ true,
        /*upDownTurnPages*/ true,
        /*okOpensBookMenu*/ true,
        /*hasLeftRight*/ false,
        /*hasUpDown*/ false,
        /*hasPowerButton*/ false,
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
