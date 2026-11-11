#include "usb_msc.h"

#include "storage.h"
#include "../../net/bookorbit_client.h"
#include "../../reader/reader.h"
#include "../../services/library_manager.h"
#include "../../services/power_service.h"
#include "../../services/sync_service.h"

#if defined(ESP_PLATFORM)
#include <esp_system.h>
#endif

UsbMscHelper UsbMsc;

bool UsbMscHelper::present() const {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    return true;
#else
    return false;
#endif
}

bool UsbMscHelper::beginSession(AppContext& ctx) {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    if (_active) return true;

    SyncService::cancelBackground();
    BookOrbit.disconnectWiFi();
    Reader.stopBackground();
    if (Reader.isOpened()) {
        LibraryManager::getInstance().flushReaderPosition();
        Reader.closeBook();
    }
    ctx.config.save();

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
#if !defined(ESP_PLATFORM)
        Storage.begin();
#else
        Serial.begin(115200);
        Serial.println("[USB-MSC] UsbMassStorage::begin failed; restarting to recover FS.");
        Serial.flush();
        PowerService::setSavedRtcState(STATE_SETTINGS);
        esp_restart();
#endif
        return false;
    }

    _active = true;
    _lastState = _msc.state();
    return true;
#else
    (void)ctx;
    return false;
#endif
}

void UsbMscHelper::exitSession(AppContext& ctx) {
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
    if (_active) {
        _msc.end();
        _active = false;
    }
    _lastState = freeink::UsbMassStorageState::Idle;

#if defined(ESP_PLATFORM)
    // Switching PHY back from TinyUSB OTG to USB Serial/JTAG and cleanly
    // remounting FsVolume requires a soft reset.
    PowerService::setSavedRtcState(STATE_SETTINGS);
    delay(80);
    esp_restart();
#else
    Storage.begin();
    LibraryManager::getInstance().scanLocalBooks();
    ctx.currentState = STATE_SETTINGS;
#endif
#else
    (void)ctx;
#endif
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
