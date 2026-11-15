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

namespace {
// Display order: {item, section starting here}. Items a board lacks are skipped.
int settingsOrder(int* items, int* sections) {
    int n = 0;
    auto add = [&](int item, int sec) {
        if (item < 0) return;
        items[n] = item;
        sections[n] = sec;
        n++;
    };
    const int light = Frontlight.present() ? NUM_SETTINGS_ITEMS - 1 : -1;   // 7 on boards with a light
    add(readingSettingIndex(), 0);
    add(orientationSettingIndex(), -1);
    add(3, -1);   // sleep screen
    add(light, -1);
    add(1, 1);    // Wi-Fi
    add(2, -1);   // server
    add(6, -1);   // synchronization
    add(webSettingIndex(), -1);
    add(usbMscSettingIndex(), -1);
    add(0, 2);    // language
    add(touchWakeSettingIndex(), -1);
    add(4, -1);   // settings file
    add(5, -1);   // device information
    return n;
}
}  // namespace

int settingsItemAt(int pos) {
    int items[20], sections[20];
    const int n = settingsOrder(items, sections);
    return (pos >= 0 && pos < n) ? items[pos] : 0;
}

int settingsSectionAt(int pos) {
    int items[20], sections[20];
    const int n = settingsOrder(items, sections);
    return (pos >= 0 && pos < n) ? sections[pos] : -1;
}
