#pragma once

#include <Arduino.h>
#include <UsbMassStorage.h>
#include "../app/app_state.h"

// Wraps FreeInk's UsbMassStorage lifecycle: stops background SD readers/writers,
// detaches FsVolume for raw block access, hands off USB PHY cleanly on ESP32-S3,
// monitors host state transitions, and reboots (or remounts on simulator) on exit.
class UsbMscHelper {
public:
    bool present() const;
    bool active() const { return _active; }

    bool beginSession(AppContext& ctx);
    void exitSession(AppContext& ctx);

    freeink::UsbMassStorageState state() const;
    bool hostConnected() const;
    bool hostSuspended() const;
    // Returns true once when state() changed since last redraw/poll.
    bool pollStateChanged();

private:
    freeink::UsbMassStorage _msc;
    bool _active = false;
    freeink::UsbMassStorageState _lastState = freeink::UsbMassStorageState::Idle;
};

extern UsbMscHelper UsbMsc;
