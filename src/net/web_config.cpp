#include "web_config.h"
#include "../ui/kit/ui_kit.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_random.h>
#include "platform/hal/display.h"
#include "platform/hal/buttons.h"
#include "services/power_service.h"
#include "platform/hal/storage.h"
#include "app/app_state.h"
#include "ui/font.h"
#include "ui/i18n.h"

WebConfigManager WebConfig;

WebConfigManager::WebConfigManager() {}

static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name='viewport' content='width=device-width, initial-scale=1'>
<meta charset='utf-8'>
<title>%HTML_TITLE%</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f4f6f8; margin: 0; padding: 16px; color: #222; }
.card { background: #fff; max-width: 440px; margin: 0 auto; border-radius: 12px; padding: 24px; box-shadow: 0 4px 16px rgba(0,0,0,0.08); }
h1 { font-size: 20px; margin-top: 0; margin-bottom: 8px; color: #111; }
p.desc { font-size: 14px; color: #666; margin-bottom: 20px; line-height: 1.4; }
.form-group { margin-bottom: 16px; }
label { display: block; font-size: 13px; font-weight: 600; margin-bottom: 6px; color: #444; }
input[type='text'], input[type='password'] { width: 100%; box-sizing: border-box; padding: 11px 12px; border: 1px solid #ccc; border-radius: 6px; font-size: 15px; }
input:focus { border-color: #000; outline: none; }
button { width: 100%; background: #000; color: #fff; border: none; padding: 14px; font-size: 16px; font-weight: 600; border-radius: 6px; cursor: pointer; margin-top: 10px; }
button:active { background: #333; }
.footer { text-align: center; font-size: 12px; color: #888; margin-top: 20px; }
</style>
</head>
<body>
<div class='card'>
<h1>%HTML_HEADING%</h1>
<p class='desc'>%HTML_SUBHEAD%</p>
<form method='POST' action='/save'>
<div class='form-group'>
<label>%LBL_SERVER%</label>
<input type='text' name='server_url' value='%SERVER_URL%' placeholder='http://192.168.1.100:3000' required>
</div>
<div class='form-group'>
<label>%LBL_USER%</label>
<input type='text' name='username' value='%USERNAME%' placeholder='user' required>
</div>
<div class='form-group'>
<label>%LBL_PASS%</label>
<input type='password' name='password' value='' placeholder='%PASS_HINT%'>
</div>
<hr style='border:none; border-top:1px solid #eee; margin:20px 0;'>
<div class='form-group'>
<label>%LBL_SSID%</label>
<input type='text' name='wifi_ssid' value='%WIFI_SSID%' placeholder='Wi-Fi SSID' required>
</div>
<div class='form-group'>
<label>%LBL_WIFI_PASS%</label>
<input type='password' name='wifi_pass' value='' placeholder='%PASS_HINT%'>
</div>
<hr style='border:none; border-top:1px solid #eee; margin:20px 0;'>
<label>%LBL_SYNC%</label>
%SYNC_TOGGLES%
<button type='submit'>%BTN_SAVE%</button>
</form>
<div class='footer'>%HTML_FOOTER%</div>
</div>
</body>
</html>
)rawliteral";

static const char HTML_SAVED[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
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
static String htmlEscape(const String& s) {
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

static void drawPortalScreen(const char* apPass) {
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::WEB_TITLE));
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_DONE), "", "", ""}, tr(MsgId::WEB_FOOTER_EXIT));
    area = insetXY(area, kMargin, 12);
    frameRect(area, Color::Black, 2);
    Rect in = insetXY(area, 14, 18);

    struct Step { MsgId title; MsgId value; bool boxed; };
    const Step steps[3] = {
        { MsgId::WEB_STEP1_TITLE, MsgId::WEB_STEP1_SSID, true },
        { MsgId::WEB_STEP2_TITLE, MsgId::WEB_STEP2_URL, true },
        { MsgId::WEB_STEP3_TITLE, MsgId::WEB_STEP3_DESC, false },
    };
    int stepNo = 0;
    for (const Step& st : steps) {
        Rect band = takeTop(in, (int16_t)(lineHeight(FontBody) + 10));
        fill(band, Color::Black, 4);
        char badge[24];
        snprintf(badge, sizeof(badge), tr(MsgId::WEB_STEP_BADGE_FMT), ++stepNo);
        text(insetX(band, 10), badge, FontBody, Color::White);
        takeTop(in, 8);
        const int16_t th = wrappedHeight(tr(st.title), FontSmall, in.width, 2);
        text(takeTop(in, th), tr(st.title), FontSmall, Color::Black, TextAlign::Left, 2);
        takeTop(in, 6);
        if (st.boxed) {
            Rect box = takeTop(in, (int16_t)(lineHeight(FontBody) + 22));
            frameRect(box, Color::Black);
            text(insetX(box, 12), tr(st.value), FontBody);
            if (st.value == MsgId::WEB_STEP1_SSID) {   // the hotspot's password, under its name
                char passLine[64];
                snprintf(passLine, sizeof(passLine), tr(MsgId::WEB_STEP1_PASS_FMT), apPass);
                takeTop(in, 4);
                Rect pbox = takeTop(in, (int16_t)(lineHeight(FontBody) + 22));
                frameRect(pbox, Color::Black);
                text(insetX(pbox, 12), passLine, FontTitle);
            }
        } else {
            const int16_t vh = wrappedHeight(tr(st.value), FontBody, in.width, 3);
            text(takeTop(in, vh), tr(st.value), FontBody, Color::Black, TextAlign::Left, 3);
        }
        takeTop(in, 22);
    }
    present(true);
}

bool WebConfigManager::runPortal(AppConfig& cfg) {
    String& serverUrl = cfg.serverHost;
    String& username = cfg.bookOrbitUser;
    String& password = cfg.bookOrbitPass;
    String& wifiSsid = cfg.wifiSsid;
    String& wifiPass = cfg.wifiPass;
    String& authToken = cfg.authToken;
    // The four moments of the Synchronization settings, as checkboxes.
    struct Toggle { const char* name; bool& value; MsgId label; };
    Toggle toggles[4] = { { "sync_on_close", cfg.syncOnClose, MsgId::SYNC_ITEM_CLOSE },
                          { "sync_on_sleep", cfg.syncOnSleep, MsgId::SYNC_ITEM_SLEEP },
                          { "sync_on_wake", cfg.syncOnWake, MsgId::SYNC_ITEM_WAKE },
                          { "sync_on_boot", cfg.syncOnBoot, MsgId::SYNC_ITEM_BOOT } };

    Serial.println("[WebConfig] Starting Web Configuration Portal...");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    
    // A password of its own each time (shown on the screen): the form carries the
    // account and Wi-Fi settings, so the hotspot is not open to anyone nearby.
    const char* apSsid = "BookOrbit-Config";
    char apPass[9];
    for (int i = 0; i < 8; i++) apPass[i] = (char)('0' + esp_random() % 10);
    apPass[8] = '\0';
    bool apOk = WiFi.softAP(apSsid, apPass);
    if (!apOk) {
        Serial.println("[WebConfig] Failed to start SoftAP!");
        return false;
    }

    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WebConfig] SoftAP '%s' started at IP: %s\n", apSsid, apIP.toString().c_str());

    DNSServer dnsServer;
    dnsServer.start(53, "*", apIP);

    drawPortalScreen(apPass);

    WebServer server(80);
    bool saved = false;

    auto handleRedirect = [&]() {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
    };

    server.on("/hotspot-detect.html", handleRedirect);
    server.on("/generate_204", handleRedirect);
    server.on("/gen_204", handleRedirect);
    server.on("/connecttest.txt", handleRedirect);
    server.on("/ncsi.txt", handleRedirect);

    server.on("/", HTTP_GET, [&]() {
        PowerService::noteActivity();
        String page = FPSTR(HTML_PAGE);
        page.replace("%HTML_TITLE%", tr(MsgId::WEB_HTML_TITLE));
        page.replace("%HTML_HEADING%", tr(MsgId::WEB_HTML_HEADING));
        page.replace("%HTML_SUBHEAD%", tr(MsgId::WEB_HTML_SUBHEAD));
        page.replace("%LBL_SERVER%", tr(MsgId::WEB_HTML_SERVER_URL));
        page.replace("%LBL_USER%", tr(MsgId::WEB_HTML_USERNAME));
        page.replace("%LBL_PASS%", tr(MsgId::WEB_HTML_PASSWORD));
        page.replace("%LBL_SSID%", tr(MsgId::WEB_HTML_WIFI_SSID));
        page.replace("%LBL_WIFI_PASS%", tr(MsgId::WEB_HTML_WIFI_PASS));
        page.replace("%BTN_SAVE%", tr(MsgId::WEB_HTML_BTN_SAVE));
        page.replace("%HTML_FOOTER%", tr(MsgId::WEB_HTML_FOOTER));
        page.replace("%DEVICE%", uikit::deviceDisplayName());

        // The saved passwords are never sent back to the browser.
        page.replace("%PASS_HINT%", htmlEscape(tr(MsgId::WEB_HTML_PASS_KEEP)));
        page.replace("%SERVER_URL%", htmlEscape(serverUrl));
        page.replace("%USERNAME%", htmlEscape(username));
        page.replace("%WIFI_SSID%", htmlEscape(wifiSsid));
        page.replace("%LBL_SYNC%", htmlEscape(tr(MsgId::WEB_HTML_SYNC_HEADING)));
        String boxes;
        for (const Toggle& t : toggles) {
            boxes += "<div class='form-group' style='margin-bottom:12px;'><label style='display:flex; align-items:center; "
                     "font-weight:normal; cursor:pointer;'><input type='checkbox' name='";
            boxes += t.name;
            boxes += "' value='1' ";
            boxes += t.value ? "checked " : "";
            boxes += "style='width:18px; height:18px; margin-right:10px; cursor:pointer;'>";
            boxes += htmlEscape(tr(t.label));
            boxes += "</label></div>\n";
        }
        page.replace("%SYNC_TOGGLES%", boxes);
        server.send(200, "text/html; charset=utf-8", page);
    });

    server.on("/save", HTTP_POST, [&]() {
        PowerService::noteActivity();
        const String oldSsid = wifiSsid;
        if (server.hasArg("server_url")) serverUrl = server.arg("server_url");
        if (server.hasArg("username"))   username = server.arg("username");
        if (server.hasArg("wifi_ssid"))  wifiSsid = server.arg("wifi_ssid");
        wifiSsid.trim();
        // An empty password field keeps the saved one (the page never shows it);
        // another network with an empty field is an open one. Passwords are taken
        // as typed: spaces in them are significant.
        if (server.hasArg("password") && server.arg("password").length() > 0) password = server.arg("password");
        if (server.hasArg("wifi_pass") && server.arg("wifi_pass").length() > 0) wifiPass = server.arg("wifi_pass");
        else if (wifiSsid != oldSsid) wifiPass = "";

        for (Toggle& t : toggles) t.value = server.hasArg(t.name);

        authToken = "";

        serverUrl.trim();
        if (serverUrl.length() > 0 && !serverUrl.startsWith("http://") && !serverUrl.startsWith("https://")) {
            serverUrl = "http://" + serverUrl;
        }
        username.trim();

        cfg.save();

        String savedPage = FPSTR(HTML_SAVED);
        savedPage.replace("%SAVED_TITLE%", tr(MsgId::WEB_HTML_SAVED_TITLE));
        savedPage.replace("%SAVED_DESC%", tr(MsgId::WEB_HTML_SAVED_DESC));
        savedPage.replace("%SAVED_HINT%", tr(MsgId::WEB_HTML_SAVED_HINT));

        server.send(200, "text/html; charset=utf-8", savedPage);
        saved = true;
        Serial.println("[WebConfig] New settings saved from web portal!");
    });

    server.onNotFound(handleRedirect);

    while (!saved) {
        dnsServer.processNextRequest();
        server.handleClient();

        ButtonEvent event;
        if (PowerService::pollModalEvent(event)) {
            if (event.action == ACTION_CLICK && (event.id == BTN_BACK || event.id == BTN_TAP)) {  // tap: Picco
                Serial.println("[WebConfig] Cancelled by user.");
                break;
            }
        }
    }

    if (saved) {
        delay(1500);
    }

    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WebConfig] Web Configuration Portal closed.");

    return saved;
}
