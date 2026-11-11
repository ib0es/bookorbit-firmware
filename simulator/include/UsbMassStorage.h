#pragma once

// Simulator stub for freeink::UsbMassStorage (matches FreeInk SDK API).
#include "SDCardManager.h"

namespace freeink {

enum class UsbMassStorageState : uint8_t {
    Idle,
    WaitingForHost,
    Connected,
    Accessed,
    Ejected,
    Disconnected,
    IoError,
};

#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC

class UsbMassStorage {
public:
    bool begin(FsBlockDeviceInterface* dev) {
        if (!dev || s_failBegin) return false;
        _active = true;
        _state = s_nextState;
        s_activeInstance = this;
        return true;
    }

    void end() {
        _active = false;
        _state = UsbMassStorageState::Idle;
        if (s_activeInstance == this) s_activeInstance = nullptr;
    }

    bool active() const { return _active; }
    UsbMassStorageState state() const { return _active ? _state : UsbMassStorageState::Idle; }
    bool hostConnected() const {
        const auto s = state();
        return s == UsbMassStorageState::Connected || s == UsbMassStorageState::Accessed;
    }
    bool hostSuspended() const { return false; }
    bool disconnectHost() const { return _active; }

    static void simSetState(UsbMassStorageState st) {
        s_nextState = st;
        if (s_activeInstance) s_activeInstance->_state = st;
    }
    static void simSetFailBegin(bool fail) { s_failBegin = fail; }
    static void simReset() {
        s_nextState = UsbMassStorageState::WaitingForHost;
        s_failBegin = false;
    }

private:
    static inline UsbMassStorageState s_nextState = UsbMassStorageState::WaitingForHost;
    static inline bool s_failBegin = false;
    static inline UsbMassStorage* s_activeInstance = nullptr;

    bool _active = false;
    UsbMassStorageState _state = UsbMassStorageState::Idle;
};

#else

class UsbMassStorage {
public:
    bool begin(FsBlockDeviceInterface*) { return false; }
    void end() {}
    bool active() const { return false; }
    UsbMassStorageState state() const { return UsbMassStorageState::Idle; }
    bool hostConnected() const { return false; }
    bool hostSuspended() const { return false; }
    bool disconnectHost() const { return false; }
    static void simSetState(UsbMassStorageState) {}
    static void simSetFailBegin(bool) {}
    static void simReset() {}
};

#endif

}  // namespace freeink
