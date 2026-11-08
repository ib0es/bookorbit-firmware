#include "app_state.h"
#include "../hal/frontlight.h"
#include "../hal/storage.h"

bool AppConfig::load() {
    return Storage.loadConfig(*this);
}

bool AppConfig::save() {
    return Storage.saveConfig(*this);
}

int settingsItemCount() { return NUM_SETTINGS_ITEMS + (Frontlight.present() ? 1 : 0); }
