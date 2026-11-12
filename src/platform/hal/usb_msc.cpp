#include "usb_msc.h"

#include "storage.h"

UsbMscHelper UsbMsc;

bool UsbMscHelper::present() const {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    return true;
#else
    return false;
#endif
}

bool UsbMscHelper::start() {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    if (_active) return true;

    FsBlockDeviceInterface* rawDev = Storage.detachForUsbMsc();
    if (!rawDev) {
        Serial.println("[USB-MSC] Cannot start: SD raw block device unavailable.");
        return false;
    }

    Serial.println("[USB-MSC] Detaching filesystem and starting USB Mass Storage...");
    Serial.flush();
#if defined(ESP_PLATFORM) && defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    // In HWCDC USB mode, Serial/JTAG holds the internal USB PHY; release it
    // before USB.begin() switches the PHY to TinyUSB OTG for MSC.
    Serial.end();
    delay(20);
#endif

    if (!_msc.begin(rawDev)) {
#if defined(ESP_PLATFORM)
        Serial.begin(115200);
#endif
        Serial.println("[USB-MSC] UsbMassStorage::begin failed.");
        return false;
    }

    _active = true;
    _lastState = _msc.state();
    return true;
#else
    return false;
#endif
}

void UsbMscHelper::stop() {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    // Boards without mass storage get the SDK's stub, which has no end().
    if (_active) _msc.end();
#endif
    _active = false;
    _lastState = freeink::UsbMassStorageState::Idle;
}

freeink::UsbMassStorageState UsbMscHelper::state() const {
    return _active ? _msc.state() : freeink::UsbMassStorageState::Idle;
}

bool UsbMscHelper::hostConnected() const {
    return _active && _msc.hostConnected();
}

bool UsbMscHelper::hostSuspended() const {
    return _active && _msc.hostSuspended();
}

bool UsbMscHelper::pollStateChanged() {
    if (!_active) return false;
    const auto st = _msc.state();
    if (st == _lastState) return false;
    _lastState = st;
    return true;
}
