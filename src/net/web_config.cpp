#include "web_config.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include <algorithm>
#include <vector>

#include "app/app_state.h"
#include "../core/time_utils.h"
#include "../core/ui_port.h"
#include "../platform/device.h"
#include "platform/hal/buttons.h"
#include "platform/hal/display.h"
#include "platform/hal/frontlight.h"
#include "platform/hal/imu.h"
#include "platform/hal/storage.h"
#include "../reader/book_reader.h"
#include "../reader/font_store.h"
#include "../reader/reader.h"
#include "services/power_service.h"
#include "ui/font.h"
#include "ui/i18n.h"

WebConfigManager WebConfig;

WebConfigManager::WebConfigManager() {}

namespace {

const char HTML_HEAD[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='utf-8'>
<title>%HTML_TITLE%</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; margin: 0; padding: 14px; color: #222; }
.card { background: #fff; max-width: 480px; margin: 0 auto; border-radius: 12px; padding: 20px; box-shadow: 0 4px 16px rgba(0,0,0,0.08); }
h1 { font-size: 20px; margin: 0 0 6px; color: #111; }
p.desc { font-size: 13px; color: #666; margin: 0 0 16px; line-height: 1.4; }
details { border: 1px solid #e5e7eb; border-radius: 8px; padding: 10px 14px; margin-bottom: 12px; background: #fafafa; }
summary { font-size: 15px; font-weight: 700; color: #111; cursor: pointer; list-style: none; display: flex; justify-content: space-between; align-items: center; }
summary::-webkit-details-marker { display: none; }
summary::after { content: '▾'; font-size: 13px; color: #666; }
details:not([open]) summary::after { content: '▸'; }
.sec-body { margin-top: 12px; }
.fg { margin-bottom: 12px; }
.fg:last-child { margin-bottom: 2px; }
label.lbl { display: block; font-size: 13px; font-weight: 600; margin-bottom: 5px; color: #444; }
input[type='text'], input[type='password'], select { width: 100%; box-sizing: border-box; padding: 10px 11px; border: 1px solid #ccc; border-radius: 6px; font-size: 15px; background: #fff; color: #111; }
input:focus, select:focus { border-color: #000; outline: none; }
label.chk { display: flex; align-items: center; font-size: 14px; font-weight: 400; color: #222; cursor: pointer; margin-bottom: 10px; }
label.chk input { width: 18px; height: 18px; margin: 0 10px 0 0; flex-shrink: 0; }
button { width: 100%; background: #000; color: #fff; border: none; padding: 14px; font-size: 16px; font-weight: 600; border-radius: 8px; cursor: pointer; margin-top: 6px; }
button:active { background: #333; }
.footer { text-align: center; font-size: 12px; color: #888; margin-top: 16px; }
</style>
</head>
<body>
<div class='card'>
<h1>%HTML_HEADING%</h1>
<p class='desc'>%HTML_SUBHEAD%</p>
<form method='POST' action='/save'>
)rawliteral";

const char HTML_TAIL[] PROGMEM = R"rawliteral(<button type='submit'>%BTN_SAVE%</button>
</form>
<div class='footer'>%HTML_FOOTER%</div>
</div>
</body>
</html>
)rawliteral";

const char HTML_SAVED[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='utf-8'>
<title>%SAVED_TITLE%</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; margin: 0; padding: 24px; color: #222; text-align: center; }
.card { background: #fff; max-width: 400px; margin: 40px auto; border-radius: 12px; padding: 32px 24px; box-shadow: 0 4px 16px rgba(0,0,0,0.08); }
h1 { font-size: 22px; color: #15803d; margin-top: 0; }
p { font-size: 15px; color: #555; line-height: 1.5; }
</style>
</head>
<body>
<div class='card'>
<h1>✓ %SAVED_TITLE%</h1>
<p>%SAVED_DESC%</p>
<p>%SAVED_HINT%</p>
</div>
</body>
</html>
)rawliteral";

// Values put into the form: a quote or '<' in a name must not break the page.
String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        const char c = s[i];
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\'': out += "&#39;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

void beginSection(String& out, const char* title, bool open) {
    out += open ? "<details open><summary>" : "<details><summary>";
    out += htmlEscape(title);
    out += "</summary><div class='sec-body'>\n";
}

void endSection(String& out) { out += "</div></details>\n"; }

void addInput(String& out, const char* type, const char* name, const char* label, const String& value,
              const char* placeholder) {
    out += "<div class='fg'><label class='lbl'>";
    out += htmlEscape(label);
    out += "</label><input type='";
    out += type;
    out += "' name='";
    out += name;
    out += "' value='";
    out += htmlEscape(value);
    out += "' placeholder='";
    out += htmlEscape(placeholder);
    out += "'></div>\n";
}

void addCheckbox(String& out, const char* name, const char* label, bool checked) {
    out += "<label class='chk'><input type='checkbox' name='";
    out += name;
    out += "' value='1'";
    if (checked) out += " checked";
    out += ">";
    out += htmlEscape(label);
    out += "</label>\n";
}

void beginSelect(String& out, const char* name, const char* label) {
    out += "<div class='fg'><label class='lbl'>";
    out += htmlEscape(label);
    out += "</label><select name='";
    out += name;
    out += "'>\n";
}

void addOption(String& out, const String& value, const String& text, bool selected) {
    out += "<option value='";
    out += htmlEscape(value);
    out += "'";
    if (selected) out += " selected";
    out += ">";
    out += htmlEscape(text);
    out += "</option>\n";
}

void endSelect(String& out) { out += "</select></div>\n"; }

// Appends a select whose discrete integer presets always include `current` (if from custom config.json).
template <typename FormatFn>
void addIntSelect(String& out, const char* name, const char* label, const int* presets, int count, int current,
                  FormatFn formatLabel) {
    std::vector<int> values(presets, presets + count);
    if (std::find(values.begin(), values.end(), current) == values.end()) values.push_back(current);

    beginSelect(out, name, label);
    for (int v : values) {
        addOption(out, String(v), formatLabel(v), v == current);
    }
    endSelect(out);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool parseIntArg(WebServer& server, const char* name, int& out) {
    if (!server.hasArg(name)) return false;
    const String raw = server.arg(name);
    if (raw.length() == 0) return false;
    char* end = nullptr;
    const long v = strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != '\0') return false;
    out = (int)v;
    return true;
}

String buildPercentText(int pct) { return String(pct) + "%"; }

String buildRefreshText(int pages) {
    if (pages <= 0) return tr(MsgId::APPEAR_REFRESH_NEVER);
    char buf[48];
    snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_REFRESH_FMT), pages);
    return buf;
}

String buildMarginText(int px) {
    char buf[24];
    snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_MARGINS_FMT), px);
    return buf;
}

String buildPortalPage(const AppConfig& cfg, const std::vector<FontStore::Family>& fonts) {
    String head = FPSTR(HTML_HEAD);
    head.replace("%HTML_TITLE%", htmlEscape(tr(MsgId::WEB_HTML_TITLE)));
    head.replace("%HTML_HEADING%", htmlEscape(tr(MsgId::WEB_HTML_HEADING)));
    head.replace("%HTML_SUBHEAD%", htmlEscape(tr(MsgId::WEB_HTML_SUBHEAD)));
    head.replace("%DEVICE%", htmlEscape(Platform::kDevice.name));

    String out;
    out.reserve(10240);
    out += head;

    const String passHint = tr(MsgId::WEB_HTML_PASS_KEEP);

    // 1. Server & Wi-Fi (open by default — primary onboarding section)
    beginSection(out, tr(MsgId::WEB_HTML_SEC_NETWORK), true);
    addInput(out, "text", "server_url", tr(MsgId::WEB_HTML_SERVER_URL), cfg.serverHost, "http://192.168.1.100:3000");
    addInput(out, "text", "username", tr(MsgId::WEB_HTML_USERNAME), cfg.bookOrbitUser, "user");
    addInput(out, "password", "password", tr(MsgId::WEB_HTML_PASSWORD), "", passHint.c_str());
    addInput(out, "text", "wifi_ssid", tr(MsgId::WEB_HTML_WIFI_SSID), cfg.wifiSsid, "Wi-Fi SSID");
    addInput(out, "password", "wifi_pass", tr(MsgId::WEB_HTML_WIFI_PASS), "", passHint.c_str());
    endSection(out);

    // 2. Sync schedule
    beginSection(out, tr(MsgId::WEB_HTML_SYNC_HEADING), false);
    addCheckbox(out, "sync_on_close", tr(MsgId::SYNC_ITEM_CLOSE), cfg.syncOnClose);
    addCheckbox(out, "sync_on_sleep", tr(MsgId::SYNC_ITEM_SLEEP), cfg.syncOnSleep);
    addCheckbox(out, "sync_on_wake", tr(MsgId::SYNC_ITEM_WAKE), cfg.syncOnWake);
    addCheckbox(out, "sync_on_boot", tr(MsgId::SYNC_ITEM_BOOT), cfg.syncOnBoot);
    endSection(out);

    // 3. Interface & Device (language, orientation, cover, light, hardware features)
    beginSection(out, tr(MsgId::WEB_HTML_SEC_DEVICE), false);
    beginSelect(out, "lang", tr(MsgId::SETTINGS_ITEM_LANGUAGE));
    for (int i = 0; i < I18n::getLanguageCount(); i++) {
        const AppLanguage l = static_cast<AppLanguage>(i);
        addOption(out, I18n::getLanguageCode(l), I18n::getLanguageName(l), cfg.language == l);
    }
    endSelect(out);

    beginSelect(out, "ui_orient", tr(MsgId::SETTINGS_ITEM_ORIENTATION));
    const MsgId uiOrientLabels[4] = {
        MsgId::APPEAR_ORIENT_PORTRAIT, MsgId::APPEAR_ORIENT_LANDSCAPE,
        MsgId::APPEAR_ORIENT_PORTRAIT_FLIPPED, MsgId::APPEAR_ORIENT_LANDSCAPE_FLIPPED
    };
    for (int i = 0; i < 4; i++) {
        addOption(out, String(i), tr(uiOrientLabels[i]), cfg.uiOrientation == i);
    }
    endSelect(out);

    if (BoardImu.present()) {
        addCheckbox(out, "auto_rotate", tr(MsgId::APPEAR_AUTO_ROTATE), cfg.autoRotate);
    }

    beginSelect(out, "sleep_cover", tr(MsgId::SETTINGS_ITEM_SLEEP_COVER));
    const String coverVal = !cfg.coverOnSleep ? "off" : (cfg.sleepCoverFitMode == 1 ? "fill" : "fit");
    addOption(out, "fit", tr(MsgId::SLEEP_COVER_MODE_FIT), coverVal == "fit");
    addOption(out, "fill", tr(MsgId::SLEEP_COVER_MODE_FILL), coverVal == "fill");
    addOption(out, "off", tr(MsgId::SLEEP_COVER_MODE_OFF), coverVal == "off");
    endSelect(out);

    if (Frontlight.present()) {
        static constexpr int kPctSteps[] = {0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100};
        constexpr int kPctCount = sizeof(kPctSteps) / sizeof(kPctSteps[0]);
        addIntSelect(out, "fl_bright", tr(MsgId::FRONTLIGHT_BRIGHTNESS), kPctSteps, kPctCount,
                     clampInt(cfg.frontlight, 0, 100), buildPercentText);
        if (Frontlight.hasWarmth()) {
            addIntSelect(out, "fl_warm", tr(MsgId::FRONTLIGHT_WARMTH), kPctSteps, kPctCount,
                         clampInt(cfg.frontlightWarm, 0, 100), buildPercentText);
        }
    }

    if (hasTouchWakeSetting()) {
        addCheckbox(out, "touch_wake", tr(MsgId::SETTINGS_ITEM_TOUCH_WAKE), cfg.touchWakeEnabled);
    }
    if (hasUsbMscSetting()) {
        addCheckbox(out, "usb_msc", tr(MsgId::USB_MSC_ITEM_TOGGLE), cfg.usbMscEnabled);
    }
    addCheckbox(out, "hide_completed", tr(MsgId::CATALOG_HIDE_COMPLETED), cfg.hideCompleted);
    endSection(out);

    // 4. Reading settings
    beginSection(out, tr(MsgId::SETTINGS_ITEM_READING), false);
    static constexpr int kRefresh[] = {1, 2, 3, 5, 8, 10, 15, 20, 0};
    addIntSelect(out, "full_refresh", tr(MsgId::APPEAR_FULL_REFRESH), kRefresh, sizeof(kRefresh) / sizeof(kRefresh[0]),
                 clampInt(cfg.fullRefreshEvery, 0, 50), buildRefreshText);

    beginSelect(out, "sharp_images", tr(MsgId::APPEAR_IMAGES));
    addOption(out, "0", tr(MsgId::APPEAR_IMAGES_DITHERED), !cfg.sharpImages);
    addOption(out, "1", tr(MsgId::APPEAR_IMAGES_SHARP), cfg.sharpImages);
    endSelect(out);

    if (!Platform::kDevice.upDownTurnPages) {
        beginSelect(out, "up_down_pages", tr(MsgId::APPEAR_UPDOWN_KEYS));
        addOption(out, "0", tr(MsgId::APPEAR_UPDOWN_CHAPTERS), !cfg.upDownTurnPages);
        addOption(out, "1", tr(MsgId::APPEAR_UPDOWN_PAGES), cfg.upDownTurnPages);
        endSelect(out);
    }
    endSection(out);

    // 5. Status bar
    beginSection(out, tr(MsgId::SB_TITLE), false);
    beginSelect(out, "sb_top", tr(MsgId::SB_TOP));
    addOption(out, "0", tr(MsgId::SB_TOP_BOOK), cfg.sbTop == 0);
    addOption(out, "1", tr(MsgId::SB_TOP_CHAPTER), cfg.sbTop == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbTop == 2);
    endSelect(out);

    beginSelect(out, "sb_pages", tr(MsgId::SB_PAGES));
    addOption(out, "0", tr(MsgId::SB_PAGES_BOOK), cfg.sbPages == 0);
    addOption(out, "1", tr(MsgId::SB_PAGES_CHAPTER), cfg.sbPages == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbPages == 2);
    endSelect(out);

    addCheckbox(out, "sb_percent", tr(MsgId::SB_PERCENT), cfg.sbPercent);
    addCheckbox(out, "sb_battery", tr(MsgId::SB_BATTERY), cfg.sbBattery);

    beginSelect(out, "sb_bottom", tr(MsgId::SB_BOTTOM));
    addOption(out, "0", tr(MsgId::SB_BOTTOM_FULL), cfg.sbBottom == 0);
    addOption(out, "1", tr(MsgId::SB_BOTTOM_BAR), cfg.sbBottom == 1);
    addOption(out, "2", tr(MsgId::SB_NONE), cfg.sbBottom == 2);
    endSelect(out);

    addCheckbox(out, "sb_clock", tr(MsgId::SB_CLOCK), cfg.sbClock);

    std::vector<int> tzOffsets;
    tzOffsets.reserve(54);
    for (int m = -720; m <= 840; m += 30) tzOffsets.push_back(m);
    const int curTz = TimeUtils::clampUtcOffset(cfg.utcOffsetMin);
    if (std::find(tzOffsets.begin(), tzOffsets.end(), curTz) == tzOffsets.end()) {
        tzOffsets.push_back(curTz);
        std::sort(tzOffsets.begin(), tzOffsets.end());
    }
    beginSelect(out, "utc_offset", tr(MsgId::SB_TIMEZONE));
    for (int m : tzOffsets) {
        char tzBuf[24];
        TimeUtils::formatUtcOffset(m, tzBuf, sizeof(tzBuf));
        addOption(out, String(m), tzBuf, m == curTz);
    }
    endSelect(out);
    endSection(out);

    // 6. Book Appearance / Typography
    beginSection(out, tr(MsgId::WEB_HTML_SEC_APPEAR), false);
    beginSelect(out, "font_family", tr(MsgId::APPEAR_TYPEFACE));
    const String builtinLabel = String(BookReader::builtinFontName()) + " (" + tr(MsgId::TYPEFACE_BUILTIN) + ")";
    addOption(out, "", builtinLabel, cfg.readerFont.length() == 0);
    bool seenCurrentFont = (cfg.readerFont.length() == 0);
    const String installed = FontStore::installedName();
    for (const auto& f : fonts) {
        const bool sel = (cfg.readerFont == f.name);
        if (sel) seenCurrentFont = true;
        addOption(out, f.name, f.name, sel);
    }
    if (!seenCurrentFont && installed == cfg.readerFont) {
        addOption(out, installed, installed, true);
    }
    endSelect(out);

    beginSelect(out, "font_size", tr(MsgId::APPEAR_SIZE));
    for (int i = 0; i < FontEngine::kSizeCount; i++) {
        addOption(out, String(i), BookReader::getFontSizeName(i), cfg.readerFontSize == i);
    }
    endSelect(out);

    static constexpr int kLineSpacing[] = {80, 90, 100, 110, 120, 130, 140, 150, 170, 200};
    addIntSelect(out, "line_spacing", tr(MsgId::APPEAR_LINE_SPACING), kLineSpacing,
                 sizeof(kLineSpacing) / sizeof(kLineSpacing[0]), clampInt(cfg.lineSpacing, 80, 200), buildPercentText);

    static constexpr int kParaSpacing[] = {0, 25, 50, 75, 100, 150, 200};
    addIntSelect(out, "para_spacing", tr(MsgId::APPEAR_PARAGRAPH_SPACING), kParaSpacing,
                 sizeof(kParaSpacing) / sizeof(kParaSpacing[0]), clampInt(cfg.paragraphSpacing, 0, 200),
                 buildPercentText);

    static constexpr int kMargins[] = {6, 10, 14, 20, 26, 32, 40, 50};
    addIntSelect(out, "margin_px", tr(MsgId::APPEAR_MARGINS), kMargins, sizeof(kMargins) / sizeof(kMargins[0]),
                 clampInt(cfg.marginPx, 4, 60), buildMarginText);

    beginSelect(out, "justify", tr(MsgId::APPEAR_ALIGNMENT));
    addOption(out, "1", tr(MsgId::APPEAR_ALIGN_JUSTIFY), cfg.justify);
    addOption(out, "0", tr(MsgId::APPEAR_ALIGN_LEFT), !cfg.justify);
    endSelect(out);

    addCheckbox(out, "hyphenation", tr(MsgId::APPEAR_HYPHENATION), cfg.hyphenation);
    addCheckbox(out, "keep_lines", tr(MsgId::APPEAR_KEEP_LINES), cfg.keepLines);
    addCheckbox(out, "book_styles", tr(MsgId::APPEAR_BOOK_STYLES), cfg.bookStyles);
    addCheckbox(out, "focus_reading", tr(MsgId::APPEAR_FOCUS_READING), cfg.focusReading);

    beginSelect(out, "book_orient", tr(MsgId::APPEAR_ORIENTATION));
    addOption(out, "0", tr(MsgId::APPEAR_ORIENT_AS_UI), cfg.orientation == 0);
    for (int i = 1; i <= 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), tr(MsgId::APPEAR_ORIENT_TURNED_FMT), i * 90);
        addOption(out, String(i), buf, cfg.orientation == i);
    }
    endSelect(out);
    endSection(out);

    String tail = FPSTR(HTML_TAIL);
    tail.replace("%BTN_SAVE%", htmlEscape(tr(MsgId::WEB_HTML_BTN_SAVE)));
    tail.replace("%HTML_FOOTER%", htmlEscape(tr(MsgId::WEB_HTML_FOOTER)));
    tail.replace("%DEVICE%", htmlEscape(Platform::kDevice.name));
    out += tail;
    return out;
}

void applySubmittedConfig(WebServer& server, AppConfig& cfg) {
    // Track credentials so authToken is only invalidated when server/user/pass actually changed.
    const String oldHost = cfg.serverHost;
    const String oldUser = cfg.bookOrbitUser;
    const String oldSsid = cfg.wifiSsid;
    bool passChanged = false;

    if (server.hasArg("server_url")) cfg.serverHost = server.arg("server_url");
    if (server.hasArg("username")) cfg.bookOrbitUser = server.arg("username");
    if (server.hasArg("wifi_ssid")) cfg.wifiSsid = server.arg("wifi_ssid");
    cfg.serverHost.trim();
    if (cfg.serverHost.length() > 0 && !cfg.serverHost.startsWith("http://") &&
        !cfg.serverHost.startsWith("https://")) {
        cfg.serverHost = "http://" + cfg.serverHost;
    }
    cfg.bookOrbitUser.trim();
    cfg.wifiSsid.trim();

    // Empty password keeps saved one unless the Wi-Fi network itself changed.
    if (server.hasArg("password") && server.arg("password").length() > 0) {
        passChanged = (cfg.bookOrbitPass != server.arg("password"));
        cfg.bookOrbitPass = server.arg("password");
    }
    if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0) {
        cfg.wifiPass = server.arg("wifi_pass");
    } else if (cfg.wifiSsid != oldSsid) {
        cfg.wifiPass = "";
    }

    if (cfg.serverHost != oldHost || cfg.bookOrbitUser != oldUser || passChanged) {
        cfg.authToken = "";
    }

    cfg.syncOnClose = server.hasArg("sync_on_close");
    cfg.syncOnSleep = server.hasArg("sync_on_sleep");
    cfg.syncOnWake = server.hasArg("sync_on_wake");
    cfg.syncOnBoot = server.hasArg("sync_on_boot");

    if (server.hasArg("lang")) {
        cfg.language = I18n::languageFromCode(server.arg("lang").c_str());
        I18n::setLanguage(cfg.language);
    }

    int v = 0;
    if (parseIntArg(server, "ui_orient", v)) cfg.uiOrientation = clampInt(v, 0, 3);
    if (BoardImu.present()) cfg.autoRotate = server.hasArg("auto_rotate");

    if (server.hasArg("sleep_cover")) {
        const String mode = server.arg("sleep_cover");
        if (mode == "off") {
            cfg.coverOnSleep = false;
        } else {
            cfg.coverOnSleep = true;
            cfg.sleepCoverFitMode = (mode == "fill") ? 1 : 0;
        }
    }

    if (Frontlight.present()) {
        if (parseIntArg(server, "fl_bright", v)) cfg.frontlight = clampInt(v, 0, 100);
        if (Frontlight.hasWarmth() && parseIntArg(server, "fl_warm", v)) {
            cfg.frontlightWarm = clampInt(v, 0, 100);
        }
    }

    if (hasTouchWakeSetting()) cfg.touchWakeEnabled = server.hasArg("touch_wake");
    if (hasUsbMscSetting()) cfg.usbMscEnabled = server.hasArg("usb_msc");
    cfg.hideCompleted = server.hasArg("hide_completed");

    if (parseIntArg(server, "full_refresh", v)) cfg.fullRefreshEvery = clampInt(v, 0, 50);
    if (parseIntArg(server, "sharp_images", v)) cfg.sharpImages = (v != 0);
    if (!Platform::kDevice.upDownTurnPages && parseIntArg(server, "up_down_pages", v)) {
        cfg.upDownTurnPages = (v != 0);
    }

    if (parseIntArg(server, "sb_top", v)) cfg.sbTop = clampInt(v, 0, 2);
    if (parseIntArg(server, "sb_pages", v)) cfg.sbPages = clampInt(v, 0, 2);
    cfg.sbPercent = server.hasArg("sb_percent");
    cfg.sbBattery = server.hasArg("sb_battery");
    if (parseIntArg(server, "sb_bottom", v)) cfg.sbBottom = clampInt(v, 0, 2);
    cfg.sbClock = server.hasArg("sb_clock");
    if (parseIntArg(server, "utc_offset", v)) cfg.utcOffsetMin = TimeUtils::clampUtcOffset(v);

    if (server.hasArg("font_family")) {
        const String want = server.arg("font_family");
        if (want.length() == 0 || want == FontStore::installedName()) {
            cfg.readerFont = want;
        } else {
            const auto fonts = FontStore::scanCard();
            const auto it = std::find_if(fonts.begin(), fonts.end(),
                                         [&](const FontStore::Family& f) { return f.name == want; });
            if (it != fonts.end()) {
                Reader.setFontFamily("");
                cfg.readerFont = FontStore::install(*it, nullptr, nullptr) ? want : String();
            }
        }
    }

    if (parseIntArg(server, "font_size", v)) cfg.readerFontSize = FontEngine::clampSize(v);
    if (parseIntArg(server, "line_spacing", v)) cfg.lineSpacing = clampInt(v, 80, 200);
    if (parseIntArg(server, "para_spacing", v)) cfg.paragraphSpacing = clampInt(v, 0, 200);
    if (parseIntArg(server, "margin_px", v)) cfg.marginPx = clampInt(v, 4, 60);
    if (parseIntArg(server, "justify", v)) cfg.justify = (v != 0);
    cfg.hyphenation = server.hasArg("hyphenation");
    cfg.keepLines = server.hasArg("keep_lines");
    cfg.bookStyles = server.hasArg("book_styles");
    cfg.focusReading = server.hasArg("focus_reading");
    if (parseIntArg(server, "book_orient", v)) cfg.orientation = clampInt(v, 0, 3);
}

}  // namespace

bool WebConfigManager::runPortal(AppConfig& cfg) {
    Serial.println("[WebConfig] Starting Web Configuration Portal...");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);

    // A password of its own each time (shown on the screen): the form carries the
    // account and Wi-Fi settings, so the hotspot is not open to anyone nearby.
    const char* apSsid = "BookOrbit-Config";
    char apPass[9];
    for (int i = 0; i < 8; i++) apPass[i] = (char)('0' + esp_random() % 10);
    apPass[8] = '\0';
    const IPAddress localIp(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(localIp, localIp, subnet);
    if (!WiFi.softAP(apSsid, apPass)) {
        Serial.println("[WebConfig] Failed to start SoftAP!");
        return false;
    }

    IPAddress apIP = WiFi.softAPIP();
    if (apIP == IPAddress((uint32_t)0)) apIP = localIp;
    const String portalUrl = String("http://") + apIP.toString() + "/";
    Serial.printf("[WebConfig] SoftAP '%s' started at IP: %s\n", apSsid, apIP.toString().c_str());

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 2)
    if (WiFi.AP.enableDhcpCaptivePortal()) {
        Serial.println("[WebConfig] DHCP Captive Portal option (RFC 8910) enabled.");
    }
#endif

    DNSServer dnsServer;
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    const bool dnsOk = dnsServer.start(53, "*", apIP);
    Serial.printf("[WebConfig] DNS server %s on %s:53\n", dnsOk ? "started" : "FAILED", apIP.toString().c_str());

    const auto fonts = FontStore::scanCard();
    WebServer server(80);
    bool saved = false;

    auto sendNoCache = [&]() {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate", true);
        server.sendHeader("Pragma", "no-cache", true);
    };

    auto handleRedirect = [&]() {
        PowerService::noteActivity();
        sendNoCache();
        server.sendHeader("Location", portalUrl, true);
        server.send(302, "text/plain", "");
    };

    server.on("/hotspot-detect.html", handleRedirect);
    server.on("/library/test/success.html", handleRedirect);
    server.on("/generate_204", handleRedirect);
    server.on("/gen_204", handleRedirect);
    server.on("/connecttest.txt", handleRedirect);
    server.on("/ncsi.txt", handleRedirect);
    server.on("/canonical.html", handleRedirect);
    server.on("/success.txt", handleRedirect);
    server.on("/redirect", handleRedirect);
    server.on("/fwlink", handleRedirect);

    server.on("/", HTTP_GET, [&]() {
        PowerService::noteActivity();
        sendNoCache();
        server.send(200, "text/html; charset=utf-8", buildPortalPage(cfg, fonts));
    });

    server.on("/save", HTTP_POST, [&]() {
        PowerService::noteActivity();
        applySubmittedConfig(server, cfg);
        cfg.save();

        String savedPage = FPSTR(HTML_SAVED);
        savedPage.replace("%SAVED_TITLE%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_TITLE)));
        savedPage.replace("%SAVED_DESC%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_DESC)));
        savedPage.replace("%SAVED_HINT%", htmlEscape(tr(MsgId::WEB_HTML_SAVED_HINT)));

        server.send(200, "text/html; charset=utf-8", savedPage);
        saved = true;
        Serial.println("[WebConfig] New settings saved from web portal!");
    });

    server.onNotFound(handleRedirect);
    server.begin();
    Serial.println("[WebConfig] HTTP server listening on port 80.");

    int page = 0;
    Ui::portalScreen(apSsid, apPass, portalUrl.c_str(), page);   // drawn by the UI (A5)
    PowerService::noteActivity();

    while (!saved) {
        dnsServer.processNextRequest();
        server.handleClient();

        ButtonEvent event;
        if (PowerService::pollModalEvent(event)) {
            const PortalAction act = Ui::portalEvent(event.id, event.action, event.x, event.y, page);
            if (act == PortalAction::Close) {
                Serial.println("[WebConfig] Cancelled by user.");
                break;
            }
            if (act == PortalAction::Redraw) {
                Ui::portalScreen(apSsid, apPass, portalUrl.c_str(), page);
            }
        }
    }

    if (saved) delay(1500);

    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WebConfig] Web Configuration Portal closed.");

    return saved;
}
