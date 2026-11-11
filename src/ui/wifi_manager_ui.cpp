#include "wifi_manager_ui.h"
#include <WiFi.h>
#include <vector>
#include "hal/display.h"
#include "hal/buttons.h"
#include "services/power_service.h"
#include "hal/battery.h"
#include "ui/font.h"
#include "ui/keyboard_ui.h"
#include "ui/i18n.h"
#include "reader/reader.h"
#include "ui/kit/ui_kit.h"
#include "ui/views/status_modal.h"
#include "net/bookorbit_client.h"

WifiManagerUI WifiUI;

static String lastEnteredSsid = "";
static String lastEnteredPass = "";

struct ScannedNet {
    String ssid;
    int rssi;
    bool isOpen;
    wifi_auth_mode_t authMode;
};

static void drawModal(const char* title, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
    drawStatusModal(title, line1, line2, line3);
}

WifiManagerUI::WifiManagerUI() {}

static void drawNetworkList(const std::vector<ScannedNet>& list, int selectedIndex, const String& currentSsid) {
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    clear();

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::WIFI_SCAN_TITLE_FMT), selectedIndex + 1, (int)list.size());
    Rect area = topBar(f, titleBuf);
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_CONNECT), tr(MsgId::KEY_RESCAN), tr(MsgId::KEY_CONNECT)});
    area = insetXY(area, kMargin, 10);

    const int16_t rowH = (int16_t)(lineHeight(FontBody) + lineHeight(FontSmall) + 20);
    const int16_t step = (int16_t)(rowH + 6);
    int perPage = (area.height + 6) / step;
    if (perPage < 1) perPage = 1;
    const int pageStart = (selectedIndex / perPage) * perPage;

    for (int i = pageStart; i < (int)list.size() && i < pageStart + perPage; i++) {
        Rect row = takeTop(area, rowH);
        takeTop(area, 6);
        const bool selected = (i == selectedIndex);
        const ScannedNet& net = list[i];
        const Color ink = selected ? Color::White : Color::Black;
        if (selected) fill(row, Color::Black);
        else frameRect(row, Color::Black);
        f.hit(row, 1, (int16_t)i);

        // RSSI: -30 (best) .. -90 (weak)
        int bars = 1;
        if (net.rssi >= -60) bars = 4;
        else if (net.rssi >= -70) bars = 3;
        else if (net.rssi >= -80) bars = 2;
        icon::signal((int16_t)(row.x + 10), (int16_t)(row.y + (row.height - 20) / 2), bars, ink);

        // Right tag
        const bool isCurrent = net.ssid.equalsIgnoreCase(currentSsid);
        const char* tag = isCurrent ? tr(MsgId::WIFI_SCAN_TAG_CURRENT)
                                    : (net.isOpen ? tr(MsgId::WIFI_SCAN_TAG_OPEN) : tr(MsgId::WIFI_SCAN_TAG_SECURED));
        Rect in = insetXY(row, 10, 8);
        const int16_t tagW = pillWidth(tag);
        pill(Rect{(int16_t)(in.right() - tagW), in.y, tagW, (int16_t)(lineHeight(FontSmall) + 6)}, tag, isCurrent,
             selected);

        Rect txt{(int16_t)(in.x + 36), in.y, (int16_t)(in.width - 36 - tagW - kGap), in.height};
        text(takeTop(txt, lineHeight(FontBody)), net.ssid.c_str(), FontBody, ink);
        char subBuf[64];
        snprintf(subBuf, sizeof(subBuf), "%d dBm  \xE2\x80\xA2  %s", net.rssi,
                 net.isOpen ? tr(MsgId::WIFI_SCAN_OPEN) : tr(MsgId::WIFI_SCAN_SECURED));
        takeTop(txt, 4);
        text(txt, subBuf, FontSmall, ink);
    }

    present(false);
}

bool WifiManagerUI::runScanAndConnect(String& outSsid, String& outPass) {
    while (true) {
        Serial.println("[WifiUI] Starting Wi-Fi network scan...");
        drawModal(tr(MsgId::WIFI_SCAN_MODAL_TITLE), tr(MsgId::WIFI_SCAN_MODAL_SCANNING), tr(MsgId::WIFI_SCAN_MODAL_WAIT));

        Reader.stopBackground();      // Wi-Fi needs the memory
        Reader.releaseFontCaches();
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(150);

        int n = WiFi.scanNetworks(false, true);
        Serial.printf("[WifiUI] Found %d raw Wi-Fi entries.\n", n);

        std::vector<ScannedNet> list;
        for (int i = 0; i < n; i++) {
            String s = WiFi.SSID(i);
            s.trim();
            if (s.length() == 0) continue;

            int r = WiFi.RSSI(i);
            wifi_auth_mode_t auth = WiFi.encryptionType(i);
            bool openNet = (auth == WIFI_AUTH_OPEN);

            bool found = false;
            for (auto& item : list) {
                if (item.ssid.equalsIgnoreCase(s)) {
                    if (r > item.rssi) {
                        item.rssi = r;
                        item.authMode = auth;
                        item.isOpen = openNet;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                list.push_back({ s, r, openNet, auth });
            }
        }
        WiFi.scanDelete();

        // Put current SSID first if found, otherwise keep list
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i].ssid.equalsIgnoreCase(outSsid)) {
                ScannedNet cur = list[i];
                list.erase(list.begin() + i);
                list.insert(list.begin(), cur);
                break;
            }
        }

        if (list.empty()) {
            drawModal(tr(MsgId::WIFI_SCAN_NONE_TITLE), tr(MsgId::WIFI_SCAN_NONE_L1), tr(MsgId::WIFI_SCAN_NONE_HINT_CONFIRM), tr(MsgId::WIFI_SCAN_NONE_HINT_BACK));
            while (true) {
                ButtonEvent event;
                if (PowerService::pollModalEvent(event) && event.action == ACTION_CLICK) {
                    if (event.id == BTN_CONFIRM || event.id == BTN_SWIPE_DOWN) break; // Rescan
                    if (event.id == BTN_BACK || event.id == BTN_TAP || event.id == BTN_SWIPE_RIGHT) {
                        BookOrbit.disconnectWiFi();
                        return false;
                    }
                }
            }
            continue; // Loop to rescan
        }

        // Selection loop
        int selectedIndex = 0;
        bool redraw = true;
        bool rescan = false;

        while (true) {
            if (redraw) {
                drawNetworkList(list, selectedIndex, outSsid);
                redraw = false;
            }

            ButtonEvent event;
            if (PowerService::pollModalEvent(event) && event.action == ACTION_CLICK) {
                if (event.id == BTN_TAP && uikit::backTapped(event.x, event.y)) event.id = BTN_BACK;
                if (event.id == BTN_TAP) {  // tap on a network row = select + connect
                    freeink::ui::ActionId action = 0;
                    int16_t value = 0;
                    if (uikit::hitTest(event.x, event.y, action, value) && value >= 0 && value < (int)list.size()) {
                        selectedIndex = value;
                        event.id = BTN_CONFIRM;
                    }
                }
                if (event.id == BTN_DOWN) {
                    selectedIndex = (selectedIndex + 1) % list.size();
                    redraw = true;
                } else if (event.id == BTN_UP) {
                    selectedIndex = (selectedIndex - 1 + list.size()) % list.size();
                    redraw = true;
                } else if (event.id == BTN_LEFT || event.id == BTN_SWIPE_DOWN) {  // swipe down: rescan (touch)
                    rescan = true;
                    break;
                } else if (event.id == BTN_BACK || event.id == BTN_SWIPE_RIGHT) {
                    BookOrbit.disconnectWiFi();
                    return false;
                } else if (event.id == BTN_CONFIRM || event.id == BTN_RIGHT) {
                    // Selected network
                    ScannedNet chosen = list[selectedIndex];

                    if (chosen.isOpen) {
                        // Open network without password
                        drawModal(tr(MsgId::WIFI_CONNECTING_TITLE), tr(MsgId::WIFI_CONNECTING_OPEN), chosen.ssid.c_str(), tr(MsgId::COMMON_WAIT));
                        if (BookOrbit.connectWiFi(chosen.ssid.c_str(), "", 12000)) {
                            outSsid = chosen.ssid;
                            outPass = "";
                            String ipStr = "IP: " + WiFi.localIP().toString();
                            drawModal(tr(MsgId::WIFI_CONNECTED_TITLE), tr(MsgId::WIFI_CONNECTED_OPEN), chosen.ssid.c_str(), ipStr.c_str());
                            PowerService::waitOrKey(2200);
                            return true;
                        } else {
                            drawModal(tr(MsgId::COMMON_ERROR), tr(MsgId::WIFI_ERR_OPEN), chosen.ssid.c_str());
                            PowerService::waitOrKey(2200);
                            redraw = true;
                        }
                    } else {
                        // Secured network
                        String passCandidate = "";

                        // If matching current saved network and has password, offer fast connect
                        if (chosen.ssid.equalsIgnoreCase(outSsid) && outPass.length() > 0) {
                            passCandidate = outPass;
                            drawModal(tr(MsgId::WIFI_CONNECTING_TITLE), tr(MsgId::WIFI_CONNECTING_SAVED), chosen.ssid.c_str());
                            if (BookOrbit.connectWiFi(chosen.ssid.c_str(), passCandidate.c_str(), 14000)) {
                                outSsid = chosen.ssid;
                                String ipStr = "IP: " + WiFi.localIP().toString();
                                drawModal(tr(MsgId::WIFI_CONNECTED_TITLE), tr(MsgId::WIFI_CONNECTED_SAVED), chosen.ssid.c_str(), ipStr.c_str());
                                PowerService::waitOrKey(2200);
                                return true;
                            } else {
                                drawModal(tr(MsgId::COMMON_ERROR), tr(MsgId::WIFI_ERR_SAVED_PASS_L1), tr(MsgId::WIFI_ERR_SAVED_PASS_L2));
                                PowerService::waitOrKey(2000);
                            }
                        } else if (chosen.ssid.equalsIgnoreCase(lastEnteredSsid)) {
                            passCandidate = lastEnteredPass;
                        }

                        // Open On-Screen Keyboard for password entry
                        bool entered = showOnScreenKeyboard(chosen.ssid, passCandidate);
                        if (!entered) {
                            redraw = true;
                            continue;
                        }

                        lastEnteredSsid = chosen.ssid;
                        lastEnteredPass = passCandidate;

                        drawModal(tr(MsgId::WIFI_CHECKING_PASS_TITLE), tr(MsgId::WIFI_CONNECTING_PREFIX), chosen.ssid.c_str(), tr(MsgId::WIFI_AUTH_WAIT));
                        if (BookOrbit.connectWiFi(chosen.ssid.c_str(), passCandidate.c_str(), 14000)) {
                            outSsid = chosen.ssid;
                            outPass = passCandidate;
                            String ipStr = "IP: " + WiFi.localIP().toString();
                            drawModal(tr(MsgId::WIFI_CONNECTED_TITLE), tr(MsgId::WIFI_CONNECTED_SAVED_MSG), chosen.ssid.c_str(), ipStr.c_str());
                            PowerService::waitOrKey(2200);
                            return true;
                        } else {
                            drawModal(tr(MsgId::WIFI_ERR_TITLE), tr(MsgId::WIFI_TEST_ERR_L1), tr(MsgId::WIFI_ERR_CHECK_PASS));
                            PowerService::waitOrKey(2500);
                            redraw = true;
                        }
                    }
                }
            }
        }

        if (rescan) continue;
        break;
    }

    BookOrbit.disconnectWiFi();
    return false;
}

// ---------------------------------------------------------------------------------
// ON-SCREEN KEYBOARD (Делегирование в KeyboardUI)
// ---------------------------------------------------------------------------------
bool WifiManagerUI::showOnScreenKeyboard(const String& ssid, String& inOutPass) {
    String subtitle = String(tr(MsgId::WIFI_KB_NET_PREFIX)) + ssid;
    return KeyboardUI::show(tr(MsgId::WIFI_KB_PASS_TITLE), subtitle, inOutPass, KB_MODE_PASSWORD);
}
