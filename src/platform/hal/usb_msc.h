#pragma once

#include <Arduino.h>
#include <UsbMassStorage.h>

// Wraps FreeInk's UsbMassStorage: detaches the card's file system for raw
// block access, hands the USB PHY over cleanly on ESP32-S3 and watches the
// host. What the app does around a session (stopping readers and sync, the
// restart afterwards) is services/usb_msc_session.
class UsbMscHelper {
public:
    bool present() const;
    bool active() const { return _active; }

    // Detaches the card and starts mass storage. false when the card has no
    // raw device or the stack did not start; the file system is then gone
    // until a remount (simulator) or a restart (device).
    bool start();
    void stop();

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
