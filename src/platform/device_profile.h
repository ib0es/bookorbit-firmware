#pragma once

// What the application needs to know about a board, in one place. One profile
// per device (platform/devices/*.h), chosen at build time by platform/device.h.
// Pins and controllers stay in the SDK's BoardConfig; this is what the firmware
// decides on top of them (which keys there are, what is on by default...).

#include <cstdint>
#include "memory_tier.h"

struct DeviceProfile {
    const char* name;            // on the sleep screen, device info, web portal
    const MemoryTier* memory;    // class of chip: platform/tiers/*.h

    // ---- input
    bool touch;                  // a touch panel: tap targets, "◀" (BACK) in the title bar
    bool touchOnly;              // no BACK / OK / LEFT / RIGHT keys: touch plus UP / DOWN
    bool upDownTurnPages;        // reading: UP / DOWN always turn pages (no chapter keys)
    bool okOpensBookMenu;        // library: OK on a book opens its menu (no LEFT key for it)
    // Centres of BACK, OK, LEFT, RIGHT along the bottom edge of a 480 px wide
    // portrait screen (the key hint tabs stand at them; scaled to wider panels).
    int16_t keyCentres[4];

    // ---- features
    bool autoRotateByDefault;    // an IMU that turns the screen, on out of the box
    bool usbMassStorage;         // the card can be lent to a computer over USB
    bool touchWake;              // a touch wakes the board from light sleep (GT911 INT)

    // ---- power
    // The SD rail switch when the SDK profile leaves it unassigned (-1: none).
    int8_t sdPowerPin;

    // ---- motion sensor
    // Quarter-turns from the sensor's axes to the X3 / X4 Classic frame the
    // auto-rotate filter expects, where the SDK's BoardConfig has no mount
    // correction for the board. Added to the user's calibration (imu_rot).
    int8_t imuMountRot;
    // The sensor's Y axis mirrored (applied before the turn, as imu_flip_y;
    // combined with the user's flip).
    bool imuMountFlipY;

    // Width of one physical key zone, in the same 480 px coordinates as keyCentres.
    int16_t keyHintWidth;
};
