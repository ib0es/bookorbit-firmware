#include "app_state.h"
#include "../hal/frontlight.h"
#include "../hal/storage.h"

bool AppConfig::load() {
    return Storage.loadConfig(*this);
}

bool AppConfig::save() {
    return Storage.saveConfig(*this);
}

bool hasUsbMscSetting() { return Platform::kDevice.usbMassStorage; }

bool hasTouchWakeSetting() { return Platform::kDevice.touchWake; }

int orientationSettingIndex() { return NUM_SETTINGS_ITEMS - 1 + (Frontlight.present() ? 1 : 0); }

int usbMscSettingIndex() { return hasUsbMscSetting() ? (orientationSettingIndex() + 1) : -1; }

int touchWakeSettingIndex() {
    if (!hasTouchWakeSetting()) return -1;
    return orientationSettingIndex() + 1 + (hasUsbMscSetting() ? 1 : 0);
}

int settingsItemCount() {
    return orientationSettingIndex() + 1 + (hasUsbMscSetting() ? 1 : 0) + (hasTouchWakeSetting() ? 1 : 0);
}
