#include "config_store.h"
#include "json_file.h"
#include "../app/app_state.h"
#include "../platform/hal/storage.h"
#include "../ui/font.h"
#include <ArduinoJson.h>
#include <Preferences.h>

bool ConfigStore::load(AppConfig& cfg) {
    bool sdLoaded = false;
    if (Storage.isAvailable()) {
        {
            JsonDocument doc;
            DeserializationError error = JsonFile::read("/config.json", doc);
            if (!error) {
                if (doc["wifi_ssid"].is<const char*>()) { cfg.wifiSsid = doc["wifi_ssid"].as<String>(); cfg.wifiSsid.trim(); }
                // Passwords as typed: spaces in them are significant (names and addresses are trimmed).
                if (doc["wifi_pass"].is<const char*>()) cfg.wifiPass = doc["wifi_pass"].as<String>();
                if (doc["server_url"].is<const char*>()) { cfg.serverHost = doc["server_url"].as<String>(); cfg.serverHost.trim(); }

                if (doc["username"].is<const char*>()) { cfg.bookOrbitUser = doc["username"].as<String>(); cfg.bookOrbitUser.trim(); }
                else if (doc["user"].is<const char*>()) { cfg.bookOrbitUser = doc["user"].as<String>(); cfg.bookOrbitUser.trim(); }

                if (doc["password"].is<const char*>()) cfg.bookOrbitPass = doc["password"].as<String>();
                else if (doc["pass"].is<const char*>()) cfg.bookOrbitPass = doc["pass"].as<String>();

                if (doc["token"].is<const char*>()) { cfg.authToken = doc["token"].as<String>(); cfg.authToken.trim(); }

                if (doc["sync_on_boot"].is<bool>()) cfg.syncOnBoot = doc["sync_on_boot"].as<bool>();
                if (doc["sync_on_sleep"].is<bool>()) cfg.syncOnSleep = doc["sync_on_sleep"].as<bool>();
                if (doc["sync_on_close"].is<bool>()) cfg.syncOnClose = doc["sync_on_close"].as<bool>();
                if (doc["sync_on_wake"].is<bool>()) cfg.syncOnWake = doc["sync_on_wake"].as<bool>();
                if (doc["cover_on_sleep"].is<bool>()) cfg.coverOnSleep = doc["cover_on_sleep"].as<bool>();
                if (doc["frontlight"].is<int>()) cfg.frontlight = doc["frontlight"].as<int>();
                if (doc["frontlight_warm"].is<int>()) cfg.frontlightWarm = doc["frontlight_warm"].as<int>();
                if (doc["hide_completed"].is<bool>()) cfg.hideCompleted = doc["hide_completed"].as<bool>();

                if (doc["cover_fit_mode"].is<int>()) {
                    int v = doc["cover_fit_mode"].as<int>();
                    cfg.sleepCoverFitMode = (v < 0 || v > 1) ? 0 : v;
                } else {
                    cfg.sleepCoverFitMode = 0;
                }
                // Book font size: "font_pt" = 12..48 (step 2). Older firmware stored
                // "font_size" = 0..3 for its four fonts (13, 15, 20, 24 px): mapped to the
                // nearest new size.
                if (doc["font_pt"].is<int>()) {
                    cfg.readerFontSize = FontEngine::clampSize((doc["font_pt"].as<int>() - 12) / 2);
                } else if (doc["font_size"].is<int>()) {
                    static const int kOld[4] = { 0, 2, 4, 6 };   // -> 12, 16, 20, 24 pt
                    const int v = doc["font_size"].as<int>();
                    cfg.readerFontSize = (v >= 0 && v <= 3) ? kOld[v] : FontEngine::kDefaultSize;
                } else {
                    cfg.readerFontSize = FontEngine::kDefaultSize;
                }

                cfg.readerFont = doc["font_family"].is<const char*>() ? doc["font_family"].as<String>() : String();
                auto intIn = [&](const char* key, int& v, int lo, int hi) {
                    if (doc[key].is<int>()) v = constrain(doc[key].as<int>(), lo, hi);
                };
                auto boolIn = [&](const char* key, bool& v) {
                    if (doc[key].is<bool>()) v = doc[key].as<bool>();
                };
                intIn("line_spacing", cfg.lineSpacing, 80, 200);
                intIn("paragraph_spacing", cfg.paragraphSpacing, 0, 200);
                intIn("margin_px", cfg.marginPx, 4, 60);
                intIn("full_refresh_every", cfg.fullRefreshEvery, 0, 50);
                boolIn("justify", cfg.justify);
                boolIn("hyphenation", cfg.hyphenation);
                boolIn("keep_lines", cfg.keepLines);
                boolIn("book_styles", cfg.bookStyles);
                boolIn("focus_reading", cfg.focusReading);
                boolIn("sharp_images", cfg.sharpImages);
                boolIn("updown_pages", cfg.upDownTurnPages);
                intIn("orientation", cfg.orientation, 0, 3);
                intIn("ui_orientation", cfg.uiOrientation, 0, 3);
                boolIn("auto_rotate", cfg.autoRotate);
                intIn("imu_rot", cfg.imuRot, 0, 3);
                boolIn("imu_flip_x", cfg.imuFlipX);
                boolIn("imu_flip_y", cfg.imuFlipY);
                intIn("sb_top", cfg.sbTop, 0, 2);
                intIn("sb_pages", cfg.sbPages, 0, 2);
                intIn("sb_bottom", cfg.sbBottom, 0, 2);
                boolIn("sb_percent", cfg.sbPercent);
                boolIn("sb_battery", cfg.sbBattery);
                boolIn("sb_clock", cfg.sbClock);
                intIn("utc_offset_min", cfg.utcOffsetMin, -720, 840);
                boolIn("usb_msc", cfg.usbMscEnabled);
                boolIn("touch_wake", cfg.touchWakeEnabled);

                if (doc["language"].is<const char*>()) {
                    cfg.language = I18n::languageFromCode(doc["language"].as<String>());
                } else {
                    cfg.language = I18n::languageFromCode(DEFAULT_LANGUAGE);
                }

                Serial.println("[Storage] Config successfully loaded from MicroSD.");
                sdLoaded = true;
            } else if (error != DeserializationError::NoMemory && error != DeserializationError::EmptyInput) {
                JsonFile::keepUnreadable("/config.json");   // a new one is written from the NVS copy below
            }
        }
    }

    // No config.json on the card (new or formatted card): the network and
    // account settings come from their NVS copy. When the card has a config,
    // it is the truth, so a cleared field (Forget network) stays cleared.
    if (!sdLoaded) {
        Preferences prefs;
        if (prefs.begin("bookorbit", true)) {
            cfg.serverHost = prefs.getString("server_url", cfg.serverHost);
            cfg.wifiSsid = prefs.getString("wifi_ssid", cfg.wifiSsid);
            cfg.wifiPass = prefs.getString("wifi_pass", cfg.wifiPass);
            cfg.bookOrbitUser = prefs.getString("username", cfg.bookOrbitUser);
            cfg.bookOrbitPass = prefs.getString("password", cfg.bookOrbitPass);
            cfg.authToken = prefs.getString("token", cfg.authToken);
            prefs.end();
            Serial.println("[Storage] No config on the card: network settings taken from NVS.");
        }
    }

    // Auto-create /config.json on SD card if it didn't exist but credentials exist in NVS
    if (!sdLoaded && Storage.isAvailable() && (cfg.serverHost.length() > 0 || cfg.wifiSsid.length() > 0)) {
        save(cfg);
    }

    return sdLoaded || (cfg.serverHost.length() > 0 || cfg.wifiSsid.length() > 0);
}

bool ConfigStore::save(const AppConfig& cfg) {
    // Mirror network and auth settings to NVS so they survive a card format or
    // swap. Every field is mirrored, empty ones too (as a removed key), and only
    // when it changed: config.json is saved often (font, light, toggles).
    Preferences prefs;
    if (prefs.begin("bookorbit", false)) {
        const struct { const char* key; const String& value; } fields[] = {
            { "server_url", cfg.serverHost }, { "wifi_ssid", cfg.wifiSsid }, { "wifi_pass", cfg.wifiPass },
            { "username", cfg.bookOrbitUser }, { "password", cfg.bookOrbitPass }, { "token", cfg.authToken } };
        for (const auto& f : fields) {
            if (f.value.length() == 0) {
                if (prefs.isKey(f.key)) prefs.remove(f.key);
            } else if (prefs.getString(f.key, "") != f.value) {
                prefs.putString(f.key, f.value);
            }
        }
        prefs.end();
    }

    if (!Storage.isAvailable()) return false;

    JsonDocument doc;
    doc["wifi_ssid"] = cfg.wifiSsid;
    doc["wifi_pass"] = cfg.wifiPass;
    doc["server_url"] = cfg.serverHost;
    doc["username"] = cfg.bookOrbitUser;
    doc["password"] = cfg.bookOrbitPass;
    doc["token"] = cfg.authToken;
    doc["sync_on_boot"] = cfg.syncOnBoot;
    doc["sync_on_sleep"] = cfg.syncOnSleep;
    doc["sync_on_close"] = cfg.syncOnClose;
    doc["sync_on_wake"] = cfg.syncOnWake;
    doc["cover_on_sleep"] = cfg.coverOnSleep;
    doc["frontlight"] = cfg.frontlight;
    doc["frontlight_warm"] = cfg.frontlightWarm;
    doc["hide_completed"] = cfg.hideCompleted;
    doc["cover_fit_mode"] = cfg.sleepCoverFitMode;
    doc["font_pt"] = FontEngine::sizePx(cfg.readerFontSize);
    doc["font_family"] = cfg.readerFont;
    doc["line_spacing"] = cfg.lineSpacing;
    doc["paragraph_spacing"] = cfg.paragraphSpacing;
    doc["margin_px"] = cfg.marginPx;
    doc["justify"] = cfg.justify;
    doc["hyphenation"] = cfg.hyphenation;
    doc["keep_lines"] = cfg.keepLines;
    doc["book_styles"] = cfg.bookStyles;
    doc["focus_reading"] = cfg.focusReading;
    doc["sharp_images"] = cfg.sharpImages;
    doc["updown_pages"] = cfg.upDownTurnPages;
    doc["orientation"] = cfg.orientation;
    doc["ui_orientation"] = cfg.uiOrientation;
    doc["auto_rotate"] = cfg.autoRotate;
    doc["imu_rot"] = cfg.imuRot;
    doc["imu_flip_x"] = cfg.imuFlipX;
    doc["imu_flip_y"] = cfg.imuFlipY;
    doc["sb_top"] = cfg.sbTop;
    doc["sb_pages"] = cfg.sbPages;
    doc["sb_bottom"] = cfg.sbBottom;
    doc["sb_percent"] = cfg.sbPercent;
    doc["sb_battery"] = cfg.sbBattery;
    doc["sb_clock"] = cfg.sbClock;
    doc["utc_offset_min"] = cfg.utcOffsetMin;
    doc["usb_msc"] = cfg.usbMscEnabled;
    doc["touch_wake"] = cfg.touchWakeEnabled;
    doc["full_refresh_every"] = cfg.fullRefreshEvery;
    doc["language"] = I18n::getLanguageCode(cfg.language);
    const bool ok = JsonFile::write("/config.json", doc, /*pretty=*/true);
    if (ok) {
        Serial.println("[Storage] Config saved to MicroSD.");
    }
    return ok;
}
