#pragma once

#include <Arduino.h>

struct AppConfig;

enum class WebPortalMode : uint8_t {
    AccessPoint = 0,
    HomeWifi = 1,
};

class WebConfigManager {
public:
    WebConfigManager();

    // Runs portal web server for uploading offline books and editing settings.
    // Supports SoftAP captive portal (AccessPoint) or existing home Wi-Fi (HomeWifi).
    // Returns true if config settings were saved at least once during the session.
    bool runPortal(AppConfig& cfg, WebPortalMode mode = WebPortalMode::AccessPoint);
};

extern WebConfigManager WebConfig;
