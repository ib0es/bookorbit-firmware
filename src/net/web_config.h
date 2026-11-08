#pragma once

#include <Arduino.h>

struct AppConfig;

class WebConfigManager {
public:
    WebConfigManager();

    // Runs captive portal web server to configure settings via smartphone or browser.
    // Edits the server / Wi-Fi / sync fields of `cfg`; on save the whole config is
    // written to /config.json (other settings such as font size are preserved).
    // Returns true if new settings were saved, false if cancelled by user.
    bool runPortal(AppConfig& cfg);
};

extern WebConfigManager WebConfig;
