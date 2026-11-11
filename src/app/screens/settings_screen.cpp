#include "screens.h"
#include "../orientation.h"
#include "../app_controller.h"
#include <WiFi.h>
#include "../../platform/hal/battery.h"
#include "../../platform/hal/buttons.h"
#include "../../platform/hal/rtc.h"
#include "../../platform/hal/imu.h"
#include "../../platform/hal/mem_stats.h"
#include "../../services/sync_clock.h"
#include "../../core/time_utils.h"
#include "../../ui/kit/ui_kit.h"
#include "../../platform/hal/frontlight.h"
#include "../../core/frontlight_policy.h"
#include "../../platform/hal/usb_msc.h"
#include "../../platform/hal/display.h"
#include "../../net/bookorbit_client.h"
#include "../../net/web_config.h"
#include "../../reader/reader.h"
#include "../../services/sync_service.h"
#include "../../ui/keyboard_ui.h"
#include "../../ui/modal.h"
#include "../../ui/wifi_manager_ui.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/status_modal.h"
#include "../../ui/views/settings_view.h"
#include "../../ui/i18n.h"
#include "../../services/usb_msc_session.h"
#include "../../ui/actions.h"

namespace {

using RedrawFn = void (*)(AppContext&, bool);
using UpdateSelectionFn = void (*)(AppContext&, int, int);

// Up/Down/Confirm/Back navigation shared by the settings menus.
void handleMenu(AppContext& ctx, const ButtonEvent& event, int& selected, int count,
                UpdateSelectionFn updateSelection, void (*execute)(AppContext&, int),
                AppState backState, RedrawFn backRedraw, bool backFull = false) {
    if (event.action != ACTION_CLICK) return;
    switch (Actions::of(event)) {
        case Action::Down: {
            int prev = selected;
            selected = (selected + 1) % count;
            if (prev != selected) {
                updateSelection(ctx, prev, selected);
            }
            break;
        }
        case Action::Up: {
            int prev = selected;
            selected = (selected - 1 + count) % count;
            if (prev != selected) {
                updateSelection(ctx, prev, selected);
            }
            break;
        }
        case Action::Select:
        case Action::Next:
            execute(ctx, selected);
            break;
        case Action::Back:
        case Action::Prev:
            ctx.currentState = backState;
            backRedraw(ctx, backFull);
            break;
        default:
            break;
    }
}

// On-screen keyboard for one config field.
void editField(AppContext& ctx, const char* title, const String& prompt, String& field, KeyboardMode mode,
               RedrawFn redraw, const String& savedLine1, bool showValue,
               void (*beforeSave)(AppContext&) = nullptr) {
    // The list shows the new value: no "Saved" message to wait out (1.5 s and two refreshes).
    (void)savedLine1;
    (void)showValue;
    if (KeyboardUI::show(title, prompt, field, mode)) {
        if (beforeSave) beforeSave(ctx);
        ctx.config.save();
    }
    redraw(ctx, false);
}

// Returns true (and has already redrawn the screen) if the user pressed BACK during a network step.
bool cancelledDuringTest(AppContext& ctx, AppState returnState) {
    if (SyncService::isCancelled() || SyncService::checkSyncCancel()) {
        SyncService::abortSync(ctx, returnState, tr(MsgId::WIFI_TEST_CANCELLED));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- main settings
void executeSetting(AppContext& ctx, int index) {
    if (index == usbMscSettingIndex()) {
        ctx.currentState = STATE_USB_MSC_SETTINGS;
        drawUsbMscSettingsScreen(ctx, false);
        return;
    }
    if (index == touchWakeSettingIndex()) {
        ctx.config.touchWakeEnabled = !ctx.config.touchWakeEnabled;
        ctx.config.save();
        updateSettingsItem(ctx, index);
        return;
    }
    if (index == orientationSettingIndex()) {
        // Cycle: (when IMU present) Auto -> Portrait -> Landscape -> Portrait flipped -> Landscape flipped -> Auto
        if (BoardImu.present()) {
            if (ctx.config.autoRotate) {
                ctx.config.autoRotate = false;
                ctx.config.uiOrientation = 0;
            } else if ((ctx.config.uiOrientation & 3) == 3) {
                ctx.config.autoRotate = true;
            } else {
                ctx.config.uiOrientation = (ctx.config.uiOrientation + 1) & 3;
            }
            BoardImu.setActive(ctx.config.autoRotate);
            AppController::getInstance().resetAutoRotate();
        } else {
            ctx.config.uiOrientation = (ctx.config.uiOrientation + 1) & 3;
        }
        ctx.config.save();
        applyOrientation(ctx.config.uiOrientation, ctx.config.orientation);   // the book's pages turn with it
        drawSettingsScreen(ctx, true);
        return;
    }
    switch (index) {
        case 0:  // Language screen
            ctx.currentState = STATE_LANGUAGE_SETTINGS;
            ctx.selectedLanguageIndex = (int)ctx.config.language;
            drawLanguageSettingsScreen(ctx, false);
            break;
        case 6:  // Synchronization submenu
            ctx.currentState = STATE_SYNC_SETTINGS;
            ctx.selectedSyncSettingIndex = 0;
            drawSyncSettingsScreen(ctx, false);
            break;
        case 7:  // Frontlight (only listed on boards that have one)
            ctx.currentState = STATE_FRONTLIGHT_SETTINGS;
            ctx.selectedFrontlightIndex = 0;
            drawFrontlightScreen(ctx, false);
            break;
        case 1:  // Wi-Fi submenu
            ctx.currentState = STATE_WIFI_SETTINGS;
            ctx.selectedWifiSettingIndex = 0;
            drawWifiSettingsScreen(ctx, false);
            break;
        case 2:  // Server submenu
            ctx.currentState = STATE_SERVER_SETTINGS;
            ctx.selectedServerSettingIndex = 0;
            drawServerSettingsScreen(ctx, false);
            break;
        case 3:  // Sleep cover submenu
            ctx.currentState = STATE_SLEEP_COVER_SETTINGS;
            if (!ctx.config.coverOnSleep) {
                ctx.selectedSleepCoverIndex = 2;
            } else if (ctx.config.sleepCoverFitMode == 1) {
                ctx.selectedSleepCoverIndex = 1;
            } else {
                ctx.selectedSleepCoverIndex = 0;
            }
            drawSleepCoverSettingsScreen(ctx, false);
            break;
        case 4:  // Settings file: reload / save
            ctx.currentState = STATE_SD_SETTINGS;
            ctx.selectedSdSettingIndex = 0;
            drawSdSettingsScreen(ctx, false);
            break;
        case 5: {  // System info
            char batStr[128];
            snprintf(batStr, sizeof(batStr), tr(MsgId::SETTINGS_INFO_BATTERY_FMT), Battery.getPercentage(), Battery.getVoltage());
            String batLine = batStr;
            if (BoardRtc.present()) {
                char clk[16], tz[16], rtcBuf[64];
                if (SyncClock::formatLocalTime(ctx.config.utcOffsetMin, clk, sizeof(clk))) {
                    TimeUtils::formatUtcOffset(ctx.config.utcOffsetMin, tz, sizeof(tz));
                    String tStr = String(clk) + " " + tz;
                    snprintf(rtcBuf, sizeof(rtcBuf), tr(MsgId::SETTINGS_INFO_RTC_FMT), tStr.c_str());
                } else {
                    snprintf(rtcBuf, sizeof(rtcBuf), "%s", tr(MsgId::SETTINGS_INFO_RTC_UNSET));
                }
                batLine += String("  ·  ") + rtcBuf;
            }
            MemStats::log("device info");
            String infoTitle = tr(MsgId::SETTINGS_INFO_TITLE);
            infoTitle.replace("%DEVICE%", uikit::deviceDisplayName());
            String controller = tr(MsgId::SETTINGS_INFO_DISPLAY);
            controller.replace("%CTRL%", Display.getControllerName());
            if (BoardImu.present()) {
                char imuBuf[64];
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                if (BoardImu.active() && BoardImu.readAccel(ax, ay, az)) {
                    snprintf(imuBuf, sizeof(imuBuf), tr(MsgId::SETTINGS_INFO_IMU_FMT), ax, ay, az);
                } else if (!BoardImu.active()) {
                    snprintf(imuBuf, sizeof(imuBuf), "%s", tr(MsgId::SETTINGS_INFO_IMU_OFF));
                } else if (BoardImu.hasLastSample()) {
                    snprintf(imuBuf, sizeof(imuBuf), tr(MsgId::SETTINGS_INFO_IMU_FMT), BoardImu.lastAx(), BoardImu.lastAy(), BoardImu.lastAz());
                } else {
                    imuBuf[0] = '\0';
                }
                if (imuBuf[0]) controller += String("  ·  ") + imuBuf;
            }
            if (Buttons.hasTouch() || Buttons.hasHomeKey()) {
                controller += Buttons.hasHomeKey() ? "  ·  Touch+Home" : "  ·  Touch";
            }
            if (hasTouchWakeSetting()) {
                controller += ctx.config.touchWakeEnabled ? "  ·  INT-Wake:On" : "  ·  INT-Wake:Off";
            }
            if (hasUsbMscSetting()) {
                controller += ctx.config.usbMscEnabled ? "  ·  USB-MSC:On" : "  ·  USB-MSC:Off";
            }
            Modal.showInfo(infoTitle, tr(MsgId::SETTINGS_INFO_FW), batLine, MemStats::summary(), controller,
                           tr(MsgId::SETTINGS_INFO_FOOTER));
            break;
        }
    }
}

// ---------------------------------------------------------------- settings file
void executeSdSetting(AppContext& ctx, int index) {
    switch (index) {
        case 0:  // Reload from SD
            if (ctx.config.load()) {
                // Everything in the file takes effect (an open book is laid out again once).
                AppController::getInstance().applyConfig();
                if (Reader.isOpened()) LibraryManager::getInstance().flushReaderPosition();
                Modal.showMessage(tr(MsgId::SETTINGS_MODAL_SD_LOAD_OK_TITLE),
                                  tr(MsgId::SETTINGS_MODAL_SD_LOAD_OK_L1),
                                  tr(MsgId::SETTINGS_MODAL_SD_LOAD_OK_L2));
            } else {
                Modal.showMessage(tr(MsgId::SETTINGS_MODAL_SD_LOAD_ERR_TITLE),
                                  tr(MsgId::SETTINGS_MODAL_SD_LOAD_ERR_L1),
                                  tr(MsgId::SETTINGS_MODAL_SD_LOAD_ERR_L2));
            }
            break;
        case 1:  // Save to SD
            if (ctx.config.save()) {
                Modal.showMessage(tr(MsgId::SETTINGS_MODAL_SD_SAVE_OK_TITLE),
                                  tr(MsgId::SETTINGS_MODAL_SD_SAVE_OK_L1),
                                  tr(MsgId::SETTINGS_MODAL_SD_SAVE_OK_L2));
            } else {
                Modal.showMessage(tr(MsgId::SETTINGS_MODAL_SD_SAVE_ERR_TITLE),
                                  tr(MsgId::SETTINGS_MODAL_SD_SAVE_ERR_L1),
                                  tr(MsgId::SETTINGS_MODAL_SD_SAVE_ERR_L2));
            }
            break;
    }
}

// ---------------------------------------------------------------- Wi-Fi settings
void testWifi(AppContext& ctx) {
    if (ctx.config.wifiSsid.length() == 0) {
        Modal.showMessage(tr(MsgId::COMMON_ERROR), tr(MsgId::WIFI_TEST_NOT_SET_L1), tr(MsgId::WIFI_TEST_NOT_SET_L2), "", "", 2000);
        return;
    }
    SyncService::resetCancel();
    drawStatusModal(tr(MsgId::WIFI_TEST_MODAL_TITLE), tr(MsgId::WIFI_TEST_MODAL_CONNECTING), ctx.config.wifiSsid.c_str(), tr(MsgId::COMMON_WAIT), nullptr, tr(MsgId::COMMON_CANCEL));
    const bool ok = BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 14000, SyncService::checkSyncCancel);
    if (cancelledDuringTest(ctx, STATE_WIFI_SETTINGS)) return;
    if (ok) {
        String ipStr = "IP: " + WiFi.localIP().toString();
        char rssiBuf[64];
        snprintf(rssiBuf, sizeof(rssiBuf), tr(MsgId::WIFI_TEST_RSSI_FMT), WiFi.RSSI());
        BookOrbit.disconnectWiFi();
        Modal.showMessage(tr(MsgId::WIFI_TEST_OK_TITLE), tr(MsgId::WIFI_TEST_OK_L1), ipStr, rssiBuf, "", 2500);
    } else {
        Modal.showMessage(tr(MsgId::WIFI_TEST_ERR_TITLE), tr(MsgId::WIFI_TEST_ERR_L1), tr(MsgId::WIFI_TEST_ERR_L2), "", "", 2500);
    }
}

void executeWifiSetting(AppContext& ctx, int index) {
    switch (index) {
        case 0:  // Scan & connect
            if (WifiUI.runScanAndConnect(ctx.config.wifiSsid, ctx.config.wifiPass)) {
                ctx.config.save();
            }
            drawWifiSettingsScreen(ctx, false);
            break;
        case 1:
            editField(ctx, tr(MsgId::WIFI_KB_SSID_TITLE), tr(MsgId::WIFI_KB_SSID_PROMPT), ctx.config.wifiSsid, KB_MODE_TEXT,
                      drawWifiSettingsScreen, tr(MsgId::WIFI_KB_SSID_SAVED), true);
            break;
        case 2: {
            char promptBuf[128];
            snprintf(promptBuf, sizeof(promptBuf), tr(MsgId::WIFI_KB_PASS_PROMPT_FMT), 
                     (ctx.config.wifiSsid.length() > 0 ? ctx.config.wifiSsid.c_str() : tr(MsgId::WIFI_KB_PASS_NONE)));
            if (KeyboardUI::show(tr(MsgId::WIFI_KB_PASS_TITLE), String(promptBuf), ctx.config.wifiPass, KB_MODE_PASSWORD)) {
                ctx.config.save();
            }
            drawWifiSettingsScreen(ctx, false);   // the row shows the (masked) password or "open"
            break;
        }
        case 3:
            testWifi(ctx);
            break;
        case 4:  // Web config portal
            WebConfig.runPortal(ctx.config);
            drawWifiSettingsScreen(ctx, false);
            break;
        case 5:  // Forget network
            if (ctx.config.wifiSsid.length() == 0 && ctx.config.wifiPass.length() == 0) {
                Modal.showMessage(tr(MsgId::WIFI_FORGET_EMPTY_TITLE), tr(MsgId::WIFI_FORGET_EMPTY_L1), "", "", "", 1500);
            } else {
                ctx.config.wifiSsid = "";
                ctx.config.wifiPass = "";
                ctx.config.save();
                Modal.showMessage(tr(MsgId::WIFI_FORGET_RESET_TITLE), tr(MsgId::WIFI_FORGET_RESET_L1), tr(MsgId::WIFI_FORGET_RESET_L2));
            }
            break;
    }
}

// ---------------------------------------------------------------- server settings
void testServer(AppContext& ctx) {
    SyncService::resetCancel();
    drawStatusModal(tr(MsgId::SERVER_TEST_TITLE), tr(MsgId::SERVER_TEST_WIFI_CONNECTING), ctx.config.wifiSsid.c_str(), nullptr, nullptr, tr(MsgId::COMMON_CANCEL));
    if (!BookOrbit.connectWiFi(ctx.config.wifiSsid.c_str(), ctx.config.wifiPass.c_str(), 15000, SyncService::checkSyncCancel)) {
        if (cancelledDuringTest(ctx, STATE_SERVER_SETTINGS)) return;
        Modal.showMessage(tr(MsgId::SERVER_TEST_WIFI_ERR_TITLE), tr(MsgId::SERVER_TEST_WIFI_ERR_L1), tr(MsgId::SERVER_TEST_WIFI_ERR_L2), "", "", 3000);
        return;
    }
    if (cancelledDuringTest(ctx, STATE_SERVER_SETTINGS)) return;

    drawStatusModal(tr(MsgId::SERVER_TEST_TITLE), tr(MsgId::SERVER_TEST_WIFI_OK), tr(MsgId::SERVER_TEST_PINGING), ctx.config.serverHost.c_str(), nullptr, tr(MsgId::COMMON_CANCEL));
    if (!BookOrbit.pingServer(ctx.config.serverHost)) {
        BookOrbit.disconnectWiFi();
        Modal.showMessage(tr(MsgId::SERVER_TEST_SERVER_ERR_TITLE), tr(MsgId::SERVER_TEST_SERVER_ERR_L1), tr(MsgId::SERVER_TEST_SERVER_ERR_L2), "", "", 3000);
        return;
    }
    if (cancelledDuringTest(ctx, STATE_SERVER_SETTINGS)) return;

    drawStatusModal(tr(MsgId::SERVER_TEST_TITLE), tr(MsgId::SERVER_TEST_SERVER_OK), tr(MsgId::SERVER_TEST_AUTH), ctx.config.bookOrbitUser.c_str(), nullptr, tr(MsgId::COMMON_CANCEL));
    String freshToken;
    const bool loggedIn = BookOrbit.login(ctx.config.serverHost, ctx.config.bookOrbitUser, ctx.config.bookOrbitPass, freshToken);
    BookOrbit.disconnectWiFi();
    if (loggedIn) {
        ctx.config.authToken = freshToken;
        ctx.config.save();
        char userBuf[64];
        snprintf(userBuf, sizeof(userBuf), tr(MsgId::SERVER_TEST_SUCCESS_USER_FMT), ctx.config.bookOrbitUser.c_str());
        Modal.showMessage(tr(MsgId::SERVER_TEST_SUCCESS_TITLE), tr(MsgId::SERVER_TEST_SUCCESS_L1), String(userBuf), "", "", 3000);
    } else {
        Modal.showMessage(tr(MsgId::SERVER_TEST_AUTH_ERR_TITLE), tr(MsgId::SERVER_TEST_AUTH_ERR_L1), tr(MsgId::SERVER_TEST_AUTH_ERR_L2), "", "", 3000);
    }
}

void invalidateToken(AppContext& ctx) { ctx.config.authToken = ""; }

void executeServerSetting(AppContext& ctx, int index) {
    switch (index) {
        case 0:
            editField(ctx, tr(MsgId::SERVER_KB_HOST_TITLE), tr(MsgId::SERVER_KB_HOST_PROMPT), ctx.config.serverHost, KB_MODE_URL,
                      drawServerSettingsScreen, tr(MsgId::SERVER_KB_HOST_SAVED), true);
            break;
        case 1:
            editField(ctx, tr(MsgId::SERVER_KB_USER_TITLE), tr(MsgId::SERVER_KB_USER_PROMPT), ctx.config.bookOrbitUser, KB_MODE_TEXT,
                      drawServerSettingsScreen, tr(MsgId::SERVER_KB_USER_SAVED), true);
            break;
        case 2: {
            editField(ctx, tr(MsgId::SERVER_KB_PASS_TITLE), tr(MsgId::SERVER_KB_PASS_PROMPT), ctx.config.bookOrbitPass, KB_MODE_PASSWORD,
                      drawServerSettingsScreen, tr(MsgId::SERVER_KB_PASS_SAVED), false, invalidateToken);
            break;
        }
        case 3:
            testServer(ctx);
            break;
    }
}

// ---------------------------------------------------------------- sync settings
void executeSyncSetting(AppContext& ctx, int index) {
    bool* flags[NUM_SYNC_SETTINGS_ITEMS] = { &ctx.config.syncOnClose, &ctx.config.syncOnSleep,
                                             &ctx.config.syncOnWake, &ctx.config.syncOnBoot };
    if (index < 0 || index >= NUM_SYNC_SETTINGS_ITEMS) return;
    *flags[index] = !*flags[index];
    ctx.config.save();
    updateSyncSettingsItem(ctx, index);
}

// ---------------------------------------------------------------- sleep cover settings
void executeSleepCoverSetting(AppContext& ctx, int index) {
    switch (index) {
        case 0:
            ctx.config.coverOnSleep = true;
            ctx.config.sleepCoverFitMode = 0;
            break;
        case 1:
            ctx.config.coverOnSleep = true;
            ctx.config.sleepCoverFitMode = 1;
            break;
        case 2:
        default:
            ctx.config.coverOnSleep = false;
            break;
    }
    ctx.config.save();
    updateSleepCoverSettingsItem(ctx, index);
}

// ---------------------------------------------------------------- USB MSC settings
void executeUsbMscSetting(AppContext& ctx, int index) {
    if (index == 0) {
        if (!ctx.config.usbMscEnabled) {
            Modal.showMessage(tr(MsgId::USB_MSC_MENU_TITLE), tr(MsgId::USB_MSC_DISABLED_L1), tr(MsgId::USB_MSC_DISABLED_L2));
            return;
        }
        if (!UsbMscSession::begin(ctx)) {
            Modal.showMessage(tr(MsgId::COMMON_ERROR), tr(MsgId::USB_MSC_ERR_NO_SD), "");
            return;
        }
        ctx.currentState = STATE_USB_MSC;
        drawUsbMscScreen(ctx, true);
        return;
    }
    if (index == 1) {
        ctx.config.usbMscEnabled = !ctx.config.usbMscEnabled;
        ctx.config.save();
        updateUsbMscSettingsItem(ctx, index);
    }
}

void exitUsbMscScreen(AppContext& ctx) {
    UsbMscSession::exit(ctx);
    if (ctx.currentState == STATE_USB_MSC) {
        ctx.currentState = STATE_SETTINGS;
    }
    drawSettingsScreen(ctx, true);
}

}  // namespace

namespace SettingsScreen {

void handleSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedSettingIndex, settingsItemCount(), updateSettingsSelection,
               executeSetting, STATE_HOME, drawHomeScreen, false);
}

void handleSdSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedSdSettingIndex, NUM_SD_SETTINGS_ITEMS, updateSdSettingsSelection,
               executeSdSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleUsbMscSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedUsbMscIndex, NUM_USB_MSC_ITEMS, updateUsbMscSettingsSelection,
               executeUsbMscSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleUsbMscEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const Action key = Actions::of(event);
    if (key == Action::Select || key == Action::Back || key == Action::Prev) {
        exitUsbMscScreen(ctx);
    }
}

void handleUsbMscTap(AppContext& ctx, int action) {
    if (action == 1) {
        exitUsbMscScreen(ctx);
    }
}

void handleWifiSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedWifiSettingIndex, NUM_WIFI_SETTINGS_ITEMS, updateWifiSettingsSelection,
               executeWifiSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleServerSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedServerSettingIndex, NUM_SERVER_SETTINGS_ITEMS, updateServerSettingsSelection,
               executeServerSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleSyncSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedSyncSettingIndex, NUM_SYNC_SETTINGS_ITEMS, updateSyncSettingsSelection,
               executeSyncSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleSleepCoverSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    handleMenu(ctx, event, ctx.selectedSleepCoverIndex, NUM_SLEEP_COVER_ITEMS, updateSleepCoverSettingsSelection,
               executeSleepCoverSetting, STATE_SETTINGS, drawSettingsScreen, false);
}

void handleLanguageSettingsEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const int langCount = I18n::getLanguageCount();
    switch (Actions::of(event)) {
        case Action::Down: {
            int prev = ctx.selectedLanguageIndex;
            ctx.selectedLanguageIndex = (ctx.selectedLanguageIndex + 1) % langCount;
            if (prev != ctx.selectedLanguageIndex) {
                updateLanguageSettingsSelection(ctx, prev, ctx.selectedLanguageIndex);
            }
            break;
        }
        case Action::Up: {
            int prev = ctx.selectedLanguageIndex;
            ctx.selectedLanguageIndex = (ctx.selectedLanguageIndex - 1 + langCount) % langCount;
            if (prev != ctx.selectedLanguageIndex) {
                updateLanguageSettingsSelection(ctx, prev, ctx.selectedLanguageIndex);
            }
            break;
        }
        case Action::Select:
        case Action::Next: {
            AppLanguage newLang = (AppLanguage)ctx.selectedLanguageIndex;
            ctx.config.language = newLang;
            I18n::setLanguage(newLang);
            ctx.config.save();
            // The settings, already in the new language, are the confirmation.
            ctx.currentState = STATE_SETTINGS;
            LibraryManager::getInstance().touch();   // the home screen's texts too
            drawSettingsScreen(ctx, false);
            break;
        }
        case Action::Back:
        case Action::Prev:
            ctx.currentState = STATE_SETTINGS;
            drawSettingsScreen(ctx, false);
            break;
        default:
            break;
    }
}

}  // namespace SettingsScreen

namespace {
int s_rememberedFrontlight = 50;

void setFrontlight(AppContext& ctx, int row, int value) {
    value = value < 0 ? 0 : (value > 100 ? 100 : value);
    if (row == 0) {
        if (value > 0) s_rememberedFrontlight = value;
        ctx.config.frontlight = value;
    } else {
        ctx.config.frontlightWarm = value;
    }
    Frontlight.apply(ctx.config.frontlight, ctx.config.frontlightWarm);
    drawFrontlightScreen(ctx, false);
}
int frontlightRows() { return Frontlight.hasWarmth() ? 2 : 1; }
int& frontlightValue(AppContext& ctx, int row) { return row == 0 ? ctx.config.frontlight : ctx.config.frontlightWarm; }
}  // namespace

namespace SettingsScreen {

void handleFrontlightEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    int& sel = ctx.selectedFrontlightIndex;
    switch (Actions::onValue(Actions::of(event))) {
        case Action::Up:
        case Action::Down:
            sel = (sel + 1) % frontlightRows();
            drawFrontlightScreen(ctx, false);
            break;
        case Action::Less:
            setFrontlight(ctx, sel, frontlightValue(ctx, sel) - 10);
            break;
        case Action::More:
            setFrontlight(ctx, sel, frontlightValue(ctx, sel) + 10);
            break;
        case Action::Select:  // light on/off
            setFrontlight(ctx, 0, FrontlightPolicy::toggleLevel(ctx.config.frontlight, s_rememberedFrontlight));
            break;
        case Action::Back:
            ctx.config.save();
            ctx.currentState = STATE_SETTINGS;
            drawSettingsScreen(ctx, false);
            break;
        default:
            break;
    }
}

void handleFrontlightTap(AppContext& ctx, int action, int value, int16_t x) {
    if (action == 5) {
        ctx.selectedFrontlightIndex = 0;
        setFrontlight(ctx, 0, FrontlightPolicy::toggleLevel(ctx.config.frontlight, s_rememberedFrontlight));
        return;
    }
    if (value < 0 || value >= frontlightRows()) return;
    ctx.selectedFrontlightIndex = value;
    int v = 0;
    switch (action) {
        case 2: setFrontlight(ctx, value, frontlightValue(ctx, value) - 10); break;
        case 3: setFrontlight(ctx, value, frontlightValue(ctx, value) + 10); break;
        case 4:
            if (frontlightTrackValue(value, x, v)) setFrontlight(ctx, value, v);
            break;
        default: drawFrontlightScreen(ctx, false); break;
    }
}

}  // namespace SettingsScreen
