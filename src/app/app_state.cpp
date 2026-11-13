#include "app_state.h"
#include "../platform/hal/frontlight.h"
#include "../storage/config_store.h"

bool AppConfig::load() { return ConfigStore::load(*this); }

bool AppConfig::save() { return ConfigStore::save(*this); }

bool hasUsbMscSetting() { return Platform::kDevice.usbMassStorage; }

bool hasTouchWakeSetting() { return Platform::kDevice.touchWake; }

int readerMenuItemCount() { return NUM_READER_MENU_ITEMS; }

// Settings: the fixed items, frontlight (boards with one), Reading, screen
// orientation, then USB drive and touch wake on the boards that have them.
int readingSettingIndex() { return NUM_SETTINGS_ITEMS - 1 + (Frontlight.present() ? 1 : 0); }
int orientationSettingIndex() { return readingSettingIndex() + 1; }

int usbMscSettingIndex() { return hasUsbMscSetting() ? (orientationSettingIndex() + 1) : -1; }

int touchWakeSettingIndex() {
    if (!hasTouchWakeSetting()) return -1;
    return orientationSettingIndex() + 1 + (hasUsbMscSetting() ? 1 : 0);
}

int webSettingIndex() {
    return orientationSettingIndex() + 1 + (hasUsbMscSetting() ? 1 : 0) + (hasTouchWakeSetting() ? 1 : 0);
}

int settingsItemCount() { return webSettingIndex() + 1; }
