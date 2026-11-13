#include <algorithm>
#include "settings_view.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../platform/hal/frontlight.h"
#include "../../platform/hal/imu.h"
#include "../../platform/hal/usb_msc.h"
#include "../../platform/hal/buttons.h"
#include "../../core/bookorbit_protocol.h"
#include "../../app/screens/appearance.h"
#include "../../app/screens/status_bar_menu.h"

using namespace uikit;

namespace {

struct SettingItemDef {
    const char* title = nullptr;
    String value;
    const char* tag = nullptr;
};

struct LangDef {
    AppLanguage lang;
    const char* name;
    const char* sub;
};

using ItemDefFn = SettingItemDef (*)(const AppContext&, int);

// One settings screen: title bar, a column of cards, the hint footer.
void drawCardScreen(const AppContext& ctx, const char* title, ItemDefFn def, int count, int selected,
                    const char* hintNav, const char* hintBack, bool fullRefresh, const char* logName) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, title);
    (void)hintNav;
    (void)hintBack;
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT), tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT)});
    area = insetXY(area, kMargin, 10);
    // Long lists (the main settings with a frontlight, touch hints taking more
    // room) get tighter cards; a landscape screen shows a page at a time.
    const CardList l = cardList(area, count, selected, (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 22));
    const int16_t cardH = l.cardH, gap = l.gap;
    const int first = l.first, perPage = l.perPage;
    for (int i = first; i < count && i < first + perPage; i++) {
        SettingItemDef item = def(ctx, i);
        card(f, takeTop(area, cardH), item.title, item.value.c_str(), item.tag, i == selected, 1, (int16_t)i);
        takeTop(area, gap);
    }
    present(fullRefresh);
    Serial.printf("[UI] %s finished in %lu ms.\n", logName, millis() - t0);
}

SettingItemDef getSettingItemDef(const AppContext& ctx, int index) {
    if (index == usbMscSettingIndex()) {
        return {
            tr(MsgId::SETTINGS_ITEM_USB_MSC),
            ctx.config.usbMscEnabled ? tr(MsgId::SETTINGS_USB_MSC_ON_SUB) : tr(MsgId::SETTINGS_USB_MSC_OFF_SUB),
            tr(MsgId::SETTINGS_TAG_USB)
        };
    }
    if (index == touchWakeSettingIndex()) {
        return {
            tr(MsgId::SETTINGS_ITEM_TOUCH_WAKE),
            ctx.config.touchWakeEnabled ? tr(MsgId::SETTINGS_TOUCH_WAKE_ON) : tr(MsgId::SETTINGS_TOUCH_WAKE_OFF),
            tr(MsgId::SETTINGS_TAG_SLEEP_LIGHT)
        };
    }
    if (index == webSettingIndex()) {
        return { tr(MsgId::SETTINGS_ITEM_WEB), tr(MsgId::SETTINGS_ITEM_WEB_SUB), tr(MsgId::SETTINGS_TAG_WEB) };
    }
    if (index == readingSettingIndex()) {
        return { tr(MsgId::SETTINGS_ITEM_READING), tr(MsgId::SETTINGS_READING_SUB), tr(MsgId::SETTINGS_TAG_READING) };
    }
    if (index == orientationSettingIndex()) {   // the interface (a book turns on top of it: Appearance)
        static const MsgId k[] = {MsgId::APPEAR_ORIENT_PORTRAIT, MsgId::APPEAR_ORIENT_LANDSCAPE,
                                  MsgId::APPEAR_ORIENT_PORTRAIT_FLIPPED, MsgId::APPEAR_ORIENT_LANDSCAPE_FLIPPED};
        const char* cur = tr(k[ctx.config.uiOrientation & 3]);
        String val = cur;
        if (BoardImu.present() && ctx.config.autoRotate) {
            val = tr(MsgId::SETTINGS_ORIENT_AUTO_FMT);
            val.replace("%s", cur);
        }
        return { tr(MsgId::SETTINGS_ITEM_ORIENTATION), val, tr(MsgId::SETTINGS_TAG_SCREEN) };
    }
    switch (index) {
        case 0:
            return { tr(MsgId::SETTINGS_ITEM_LANGUAGE), I18n::getLanguageName(ctx.config.language), tr(MsgId::SETTINGS_TAG_LANG) };
        case 1: {
            String wifiVal = ctx.config.wifiSsid.length() > 0 
                ? (String(tr(MsgId::SETTINGS_ITEM_WIFI_SUB_PREFIX)) + ctx.config.wifiSsid) 
                : String(tr(MsgId::SETTINGS_ITEM_WIFI_NONE));
            return { tr(MsgId::SETTINGS_ITEM_WIFI), wifiVal, tr(MsgId::SETTINGS_TAG_WIFI) };
        }
        case 2: {
            String serverVal = ctx.config.serverHost.length() > 0 
                ? (ctx.config.serverHost + (ctx.config.bookOrbitUser.length() > 0 ? (" (" + ctx.config.bookOrbitUser + ")") : "")) 
                : String(tr(MsgId::SETTINGS_ITEM_SERVER_NONE));
            return { tr(MsgId::SETTINGS_ITEM_SERVER), serverVal, tr(MsgId::SETTINGS_TAG_SERVER) };
        }
        case 3: {
            String sleepVal = !ctx.config.coverOnSleep 
                ? String(tr(MsgId::SLEEP_COVER_MODE_OFF)) 
                : (ctx.config.sleepCoverFitMode == 0 ? String(tr(MsgId::SLEEP_COVER_MODE_FIT)) : String(tr(MsgId::SLEEP_COVER_MODE_FILL)));
            return { tr(MsgId::SETTINGS_ITEM_SLEEP_COVER), sleepVal, tr(MsgId::SETTINGS_TAG_SLEEP) };
        }
        case 4:   // reload / save: a submenu (service actions, not settings of their own)
            return { tr(MsgId::SETTINGS_ITEM_SD_FILE), tr(MsgId::SETTINGS_ITEM_SD_FILE_SUB), tr(MsgId::SETTINGS_TAG_SD) };
        case 6: {   // Synchronization submenu: what is on, in a few words
            String on;
            const struct { bool v; MsgId m; } parts[4] = {
                { ctx.config.syncOnClose, MsgId::SYNC_SHORT_CLOSE }, { ctx.config.syncOnSleep, MsgId::SYNC_SHORT_SLEEP },
                { ctx.config.syncOnWake, MsgId::SYNC_SHORT_WAKE }, { ctx.config.syncOnBoot, MsgId::SYNC_SHORT_BOOT } };
            for (const auto& p : parts) {
                if (!p.v) continue;
                if (on.length()) on += " \xC2\xB7 ";   // middle dot
                on += tr(p.m);
            }
            if (!on.length()) on = tr(MsgId::SETTINGS_SYNC_NONE);
            return { tr(MsgId::SETTINGS_ITEM_SYNC), on, tr(MsgId::SETTINGS_TAG_SYNC) };
        }
        case 7: {
            char v[48];
            if (ctx.config.frontlight <= 0) snprintf(v, sizeof(v), "%s", tr(MsgId::SETTINGS_FRONTLIGHT_OFF));
            else snprintf(v, sizeof(v), tr(MsgId::SETTINGS_FRONTLIGHT_FMT), ctx.config.frontlight, ctx.config.frontlightWarm);
            return { tr(MsgId::SETTINGS_ITEM_FRONTLIGHT), String(v), tr(MsgId::SETTINGS_TAG_LIGHT) };
        }
        case 5:
        default:
        {
            String sub = tr(MsgId::SETTINGS_ITEM_DEVICE_INFO_SUB);
            sub.replace("%DEVICE%", deviceDisplayName());
            return { tr(MsgId::SETTINGS_ITEM_DEVICE_INFO), sub, tr(MsgId::SETTINGS_TAG_INFO) };
        }
    }
}

SettingItemDef getWifiSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return { tr(MsgId::WIFI_ITEM_SCAN), tr(MsgId::WIFI_ITEM_SCAN_SUB), tr(MsgId::WIFI_TAG_SCAN) };
        case 1:
            return { tr(MsgId::WIFI_ITEM_SSID), ctx.config.wifiSsid.length() > 0 ? ctx.config.wifiSsid : tr(MsgId::WIFI_ITEM_SSID_NONE), tr(MsgId::WIFI_TAG_ENTER) };
        case 2: {
            String maskedWifiPass = "";
            for (size_t p = 0; p < ctx.config.wifiPass.length() && p < 12; p++) maskedWifiPass += "*";
            if (maskedWifiPass.length() == 0) maskedWifiPass = tr(MsgId::WIFI_PASS_OPEN);
            return { tr(MsgId::WIFI_ITEM_PASS), maskedWifiPass, tr(MsgId::WIFI_TAG_PASS) };
        }
        case 3: {
            char testBuf[96];
            if (ctx.config.wifiSsid.length() > 0) {
                snprintf(testBuf, sizeof(testBuf), tr(MsgId::WIFI_ITEM_TEST_SUB_FMT), ctx.config.wifiSsid.c_str());
            } else {
                snprintf(testBuf, sizeof(testBuf), "%s", tr(MsgId::WIFI_ITEM_TEST_NONE));
            }
            return { tr(MsgId::WIFI_ITEM_TEST), String(testBuf), tr(MsgId::WIFI_TAG_TEST) };
        }
        case 4:
        default:
            return { tr(MsgId::WIFI_ITEM_FORGET), tr(MsgId::WIFI_ITEM_FORGET_SUB), tr(MsgId::WIFI_TAG_RESET) };
    }
}

SettingItemDef getWebSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return { tr(MsgId::WIFI_ITEM_WEB), tr(MsgId::WIFI_ITEM_WEB_SUB), tr(MsgId::WEB_MENU_TAG_AP) };
        case 1:
        default: {
            char sub[80];
            if (ctx.config.wifiSsid.length() > 0) {
                snprintf(sub, sizeof(sub), tr(MsgId::WEB_MENU_PORTAL_WIFI_SUB), ctx.config.wifiSsid.c_str());
            } else {
                snprintf(sub, sizeof(sub), "%s", tr(MsgId::WEB_MENU_PORTAL_WIFI_NO_CFG));
            }
            return { tr(MsgId::WEB_MENU_PORTAL_WIFI), String(sub), tr(MsgId::WEB_MENU_TAG_WIFI) };
        }
    }
}

SettingItemDef getServerSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return { tr(MsgId::SERVER_ITEM_HOST), ctx.config.serverHost.length() > 0 ? ctx.config.serverHost : tr(MsgId::SERVER_ITEM_HOST_NONE),
                     // B4: an address that is not https:// is flagged
                     tr(BookOrbitProtocol::isPlainHttp(ctx.config.serverHost.c_str()) ? MsgId::SERVER_TAG_HTTP : MsgId::SERVER_TAG_URL) };
        case 1:
            return { tr(MsgId::SERVER_ITEM_USER), ctx.config.bookOrbitUser.length() > 0 ? ctx.config.bookOrbitUser : tr(MsgId::SERVER_ITEM_USER_NONE), tr(MsgId::SERVER_TAG_LOGIN) };
        case 2: {
            String maskedPass = "";
            for (size_t p = 0; p < ctx.config.bookOrbitPass.length() && p < 12; p++) maskedPass += "*";
            if (maskedPass.length() == 0) maskedPass = tr(MsgId::SERVER_PASS_NONE);
            return { tr(MsgId::SERVER_ITEM_PASS), maskedPass, tr(MsgId::SERVER_TAG_PASS) };
        }
        case 3:
        default:
            return { tr(MsgId::SERVER_ITEM_TEST), tr(MsgId::SERVER_ITEM_TEST_SUB), tr(MsgId::SERVER_TAG_TEST) };
    }
}

SettingItemDef getSyncSettingItemDef(const AppContext& ctx, int index) {
    const char* off = tr(MsgId::SYNC_OFF);
    switch (index) {
        case 0: return { tr(MsgId::SYNC_ITEM_CLOSE), ctx.config.syncOnClose ? tr(MsgId::SYNC_CLOSE_ON) : off, tr(MsgId::SYNC_TAG_OPTION) };
        case 1: return { tr(MsgId::SYNC_ITEM_SLEEP), ctx.config.syncOnSleep ? tr(MsgId::SYNC_SLEEP_ON) : off, tr(MsgId::SYNC_TAG_OPTION) };
        case 2: return { tr(MsgId::SYNC_ITEM_WAKE), ctx.config.syncOnWake ? tr(MsgId::SYNC_WAKE_ON) : off, tr(MsgId::SYNC_TAG_OPTION) };
        case 3:
        default: return { tr(MsgId::SYNC_ITEM_BOOT), ctx.config.syncOnBoot ? tr(MsgId::SYNC_BOOT_ON) : off, tr(MsgId::SYNC_TAG_OPTION) };
    }
}

// Settings -> Reading: a card a row, its value below the name; the status bar
// card says what the lines above and below the text show.
SettingItemDef getReadingSettingItemDef(const AppContext& ctx, int index) {
    const int row = Appearance::Reading::rowAt(index);
    char buf[64];
    if (row == Appearance::STATUS_BAR) {
        String v = StatusBarMenu::label(StatusBarMenu::TOP);
        v += ": ";
        v += StatusBarMenu::value(ctx.config, StatusBarMenu::TOP, buf, sizeof(buf));
        v += " \xC2\xB7 ";   // middle dot
        v += StatusBarMenu::label(StatusBarMenu::BOTTOM);
        v += ": ";
        v += StatusBarMenu::value(ctx.config, StatusBarMenu::BOTTOM, buf, sizeof(buf));
        return { Appearance::label(row), v, tr(MsgId::SB_TAG) };
    }
    return { Appearance::label(row), String(Appearance::value(ctx.config, row, buf, sizeof(buf))),
             tr(MsgId::SYNC_TAG_OPTION) };
}

SettingItemDef getStatusBarItemDef(const AppContext& ctx, int index) {
    char buf[64];
    return { StatusBarMenu::label(index), String(StatusBarMenu::value(ctx.config, index, buf, sizeof(buf))),
             tr(MsgId::SYNC_TAG_OPTION) };
}

SettingItemDef getSleepCoverSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0: {
            const bool active = ctx.config.coverOnSleep && (ctx.config.sleepCoverFitMode == 0);
            return { tr(MsgId::SLEEP_COVER_MODE_FIT), tr(MsgId::SLEEP_COVER_FIT_SUB), active ? tr(MsgId::LANG_ACTIVE_BADGE) : "" };
        }
        case 1: {
            const bool active = ctx.config.coverOnSleep && (ctx.config.sleepCoverFitMode == 1);
            return { tr(MsgId::SLEEP_COVER_MODE_FILL), tr(MsgId::SLEEP_COVER_FILL_SUB), active ? tr(MsgId::LANG_ACTIVE_BADGE) : "" };
        }
        case 2:
        default: {
            const bool active = !ctx.config.coverOnSleep;
            return { tr(MsgId::SLEEP_COVER_MODE_OFF), tr(MsgId::SLEEP_COVER_OFF_SUB), active ? tr(MsgId::LANG_ACTIVE_BADGE) : "" };
        }
    }
}

SettingItemDef getSdSettingItemDef(const AppContext&, int index) {
    if (index == 0) return { tr(MsgId::SETTINGS_ITEM_RELOAD_SD), tr(MsgId::SETTINGS_ITEM_RELOAD_SD_SUB), tr(MsgId::SETTINGS_TAG_SD) };
    return { tr(MsgId::SETTINGS_ITEM_SAVE_SD), tr(MsgId::SETTINGS_ITEM_SAVE_SD_SUB), tr(MsgId::SETTINGS_TAG_SD) };
}

SettingItemDef getUsbMscSettingItemDef(const AppContext& ctx, int index) {
    if (index == 0) {
        return {
            tr(MsgId::USB_MSC_ITEM_CONNECT),
            ctx.config.usbMscEnabled ? tr(MsgId::USB_MSC_CONNECT_SUB_READY) : tr(MsgId::USB_MSC_CONNECT_SUB_OFF),
            ctx.config.usbMscEnabled ? tr(MsgId::USB_MSC_TAG_GO) : tr(MsgId::USB_MSC_MODE_OFF)
        };
    }
    return {
        tr(MsgId::USB_MSC_ITEM_TOGGLE),
        ctx.config.usbMscEnabled ? tr(MsgId::USB_MSC_MODE_ON) : tr(MsgId::USB_MSC_MODE_OFF),
        tr(MsgId::SETTINGS_TAG_USB)
    };
}

const LangDef LANG_DEFS[(size_t)AppLanguage::COUNT] = {
    { AppLanguage::EN, "English", "English (Default)" },
    { AppLanguage::RU, "Русский", "Русский язык" },
    { AppLanguage::ES, "Español", "Idioma español" }
};


SettingItemDef getLanguageItemDef(const AppContext& ctx, int i) {
    const bool active = (ctx.config.language == LANG_DEFS[i].lang);
    return { LANG_DEFS[i].name, LANG_DEFS[i].sub, active ? tr(MsgId::LANG_ACTIVE_BADGE) : "" };
}

}  // namespace

// ============================================================================
// Full screens
// ============================================================================

void drawSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::SETTINGS_TITLE), getSettingItemDef, settingsItemCount(), ctx.selectedSettingIndex,
                   tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK), fullRefresh, "drawSettingsScreen");
}

void drawWifiSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::WIFI_TITLE), getWifiSettingItemDef, NUM_WIFI_SETTINGS_ITEMS,
                   ctx.selectedWifiSettingIndex, tr(MsgId::WIFI_HINT_NAV), tr(MsgId::WIFI_HINT_BACK), fullRefresh,
                   "drawWifiSettingsScreen");
}

void drawWebSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::WEB_MENU_TITLE), getWebSettingItemDef, NUM_WEB_SETTINGS_ITEMS,
                   ctx.selectedWebSettingIndex, tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK),
                   fullRefresh, "drawWebSettingsScreen");
}

void drawServerSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::SERVER_TITLE), getServerSettingItemDef, NUM_SERVER_SETTINGS_ITEMS,
                   ctx.selectedServerSettingIndex, tr(MsgId::SERVER_HINT_NAV), tr(MsgId::SERVER_HINT_BACK),
                   fullRefresh, "drawServerSettingsScreen");
}

void drawSyncSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::SYNC_TITLE), getSyncSettingItemDef, NUM_SYNC_SETTINGS_ITEMS,
                   ctx.selectedSyncSettingIndex, tr(MsgId::SYNC_HINT_NAV), tr(MsgId::SYNC_HINT_BACK), fullRefresh,
                   "drawSyncSettingsScreen");
}

void drawReadingSettingsScreen(AppContext& ctx, bool fullRefresh) {
    int& sel = ctx.selectedReadingSettingIndex;
    const int n = Appearance::Reading::count();
    if (sel < 0 || sel >= n) sel = 0;
    drawCardScreen(ctx, tr(MsgId::READING_TITLE), getReadingSettingItemDef, n, sel, tr(MsgId::SETTINGS_HINT_NAV),
                   tr(MsgId::SETTINGS_HINT_BACK), fullRefresh, "drawReadingSettingsScreen");
}

void drawStatusBarScreen(AppContext& ctx, bool fullRefresh) {
    int& sel = ctx.selectedStatusBarIndex;
    if (sel < 0 || sel >= StatusBarMenu::COUNT) sel = 0;
    drawCardScreen(ctx, tr(MsgId::SB_SCREEN_TITLE), getStatusBarItemDef, StatusBarMenu::COUNT, sel, tr(MsgId::SETTINGS_HINT_NAV),
                   tr(MsgId::SETTINGS_HINT_BACK), fullRefresh, "drawStatusBarScreen");
}

void drawSdSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::SD_SETTINGS_TITLE), getSdSettingItemDef, NUM_SD_SETTINGS_ITEMS, ctx.selectedSdSettingIndex,
                   tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK), fullRefresh, "drawSdSettingsScreen");
}

void drawUsbMscSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::USB_MSC_MENU_TITLE), getUsbMscSettingItemDef, NUM_USB_MSC_ITEMS, ctx.selectedUsbMscIndex,
                   tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK), fullRefresh, "drawUsbMscSettingsScreen");
}

void drawUsbMscScreen(const AppContext& ctx, bool fullRefresh) {
    (void)ctx;
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::USB_MSC_SCREEN_TITLE));
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_DONE), tr(MsgId::KEY_BACK), tr(MsgId::KEY_DONE)});
    area = insetXY(area, kMargin, 16);

    const char* statusStr = tr(MsgId::USB_MSC_STATE_WAIT);
    switch (UsbMsc.state()) {
        case freeink::UsbMassStorageState::Connected:    statusStr = tr(MsgId::USB_MSC_STATE_CONN); break;
        case freeink::UsbMassStorageState::Accessed:     statusStr = tr(MsgId::USB_MSC_STATE_ACCESS); break;
        case freeink::UsbMassStorageState::Ejected:      statusStr = tr(MsgId::USB_MSC_STATE_EJECT); break;
        case freeink::UsbMassStorageState::Disconnected: statusStr = tr(MsgId::USB_MSC_STATE_DISC); break;
        case freeink::UsbMassStorageState::IoError:      statusStr = tr(MsgId::USB_MSC_STATE_ERR); break;
        case freeink::UsbMassStorageState::Idle:
        default:                                         statusStr = tr(MsgId::USB_MSC_STATE_WAIT); break;
    }

    const int16_t boxH = (int16_t)(lineHeight(FontTitle) + 32);
    Rect box = takeTop(area, boxH);
    frameRect(box, Color::Black, 2);
    text(insetXY(box, 12, 8), statusStr, FontTitle, Color::Black, TextAlign::Center, 2);
    takeTop(area, 16);

    const char* hints[2] = {
        tr(MsgId::USB_MSC_HINT_L1),
        tr(touchUi() ? MsgId::USB_MSC_HINT_L2 : MsgId::USB_MSC_HINT_L2_KEYS),
    };
    for (const char* hint : hints) {
        if (!hint || !*hint) continue;
        const int16_t lh = wrappedHeight(hint, FontBody, area.width, 3);
        text(takeTop(area, lh), hint, FontBody, Color::Black, TextAlign::Left, 3);
        takeTop(area, 12);
    }

    if (touchUi()) {
        const int16_t btnH = (int16_t)(lineHeight(FontBody) + 26);
        Rect btn = takeBottom(area, btnH);
        fill(btn, Color::Black, 4);
        text(btn, tr(MsgId::USB_MSC_BTN_EXIT), FontBody, Color::White, TextAlign::Center);
        f.hit(btn, 1, 0);
    } else {
        // Button-only boards (no touchscreen): render an outlined hardware-key cue
        // without a touch hit area or solid-black touch CTA.
        const char* exitHint = tr(MsgId::USB_MSC_KEYS_EXIT);
        const int16_t innerW = (int16_t)std::max<int>(40, area.width - 24);
        const int16_t boxTextH = wrappedHeight(exitHint, FontBody, innerW, 2);
        const int16_t btnH = std::max<int16_t>((int16_t)(lineHeight(FontBody) + 26), (int16_t)(boxTextH + 16));
        Rect boxArea = takeBottom(area, btnH);
        frameRect(boxArea, Color::Black, 1, 4);
        text(insetXY(boxArea, 12, 4), exitHint, FontBody, Color::Black, TextAlign::Center, 2);
    }

    present(fullRefresh);
}

void drawLanguageSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::LANG_SCREEN_TITLE), getLanguageItemDef, (int)AppLanguage::COUNT, ctx.selectedLanguageIndex,
                   tr(MsgId::LANG_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK), fullRefresh,
                   "drawLanguageSettingsScreen");
}

void drawSleepCoverSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawCardScreen(ctx, tr(MsgId::SLEEP_COVER_TITLE), getSleepCoverSettingItemDef, NUM_SLEEP_COVER_ITEMS,
                   ctx.selectedSleepCoverIndex, tr(MsgId::SETTINGS_HINT_NAV), tr(MsgId::SETTINGS_HINT_BACK),
                   fullRefresh, "drawSleepCoverSettingsScreen");
}

// ============================================================================
// Frontlight: a slider row per channel (brightness, and warmth when the board
// mixes warm/cool LEDs). Hit areas: 1 = row, 2 = minus, 3 = plus, 4 = track.
// ============================================================================

static Rect s_tracks[2];

bool frontlightTrackValue(int row, int16_t x, int& valueOut) {
    if (row < 0 || row > 1 || s_tracks[row].width <= 0) return false;
    const Rect t = s_tracks[row];
    int v = (int)((int32_t)(x - t.x) * 100 / t.width);
    v = v < 0 ? 0 : (v > 100 ? 100 : v);
    valueOut = ((v + 2) / 5) * 5;  // 5 % steps
    return true;
}

namespace {

// Height of the sliders and the on / off button (drawFrontlightControls).
int16_t frontlightControlsHeight() {
    const int rows = Frontlight.hasWarmth() ? 2 : 1;
    return (int16_t)(rows * (lineHeight(FontTitle) + 74 + 12) + lineHeight(FontBody) + 24);
}

// The brightness / warmth sliders and the Turn on / Turn off button, at the top of `area`.
void drawFrontlightControls(Frame& f, Rect& area, const AppContext& ctx, int selected) {
    const int rows = Frontlight.hasWarmth() ? 2 : 1;
    const MsgId labels[2] = { MsgId::FRONTLIGHT_BRIGHTNESS, MsgId::FRONTLIGHT_WARMTH };
    const int values[2] = { ctx.config.frontlight, ctx.config.frontlightWarm };
    const int16_t th = lineHeight(FontTitle);
    s_tracks[0] = s_tracks[1] = Rect{};
    for (int r = 0; r < rows; r++) {
        const bool sel = (r == selected);
        const Color ink = sel ? Color::White : Color::Black;
        Rect row = takeTop(area, (int16_t)(th + 74));
        takeTop(area, 12);
        if (sel) fill(row, Color::Black);
        else frameRect(row, Color::Black);
        f.hit(row, 1, (int16_t)r);
        Rect in = insetXY(row, 12, 10);
        Rect head = takeTop(in, th);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", values[r]);
        text(head, tr(labels[r]), FontTitle, ink);
        text(head, pct, FontTitle, ink, TextAlign::Right);
        takeTop(in, 10);
        Rect ctl = takeTop(in, 44);
        const int16_t btnW = 52;
        Rect minus{ctl.x, ctl.y, btnW, ctl.height};
        Rect plus{(int16_t)(ctl.right() - btnW), ctl.y, btnW, ctl.height};
        for (const Rect& b : { minus, plus }) fill(b, sel ? Color::White : Color::Black, 4);
        const Color btnInk = sel ? Color::Black : Color::White;
        text(minus, "-", FontLarge, btnInk, TextAlign::Center);
        text(plus, "+", FontLarge, btnInk, TextAlign::Center);
        Rect track{(int16_t)(minus.right() + 14), (int16_t)(ctl.y + ctl.height / 2 - 6),
                   (int16_t)(plus.x - minus.right() - 28), 12};
        frameRect(track, ink, 1, 6);
        const int16_t fillW = (int16_t)((int32_t)(track.width - 4) * values[r] / 100);
        if (fillW > 0) fill(Rect{(int16_t)(track.x + 2), (int16_t)(track.y + 2), fillW, (int16_t)(track.height - 4)}, ink, 4);
        s_tracks[r] = track;
        f.hit(minus, 2, (int16_t)r);
        f.hit(plus, 3, (int16_t)r);
        f.hit(Rect{track.x, ctl.y, track.width, ctl.height}, 4, (int16_t)r);
    }
    // What a press does: off -> "Turn on" (filled), on -> "Turn off" (OK on the keys).
    const bool isOn = ctx.config.frontlight > 0;
    Rect btn = takeTop(area, (int16_t)(lineHeight(FontBody) + 24));
    if (!isOn) fill(btn, Color::Black, 4);
    else frameRect(btn, Color::Black, 2);
    text(btn, tr(isOn ? MsgId::FRONTLIGHT_TURN_OFF : MsgId::FRONTLIGHT_TURN_ON), FontBody,
         isOn ? Color::Black : Color::White, TextAlign::Center);
    f.hit(btn, 5, 0);
}

}  // namespace

void drawFrontlightScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::FRONTLIGHT_TITLE));
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_DONE), tr(MsgId::KEY_ON_OFF), "-", "+"});
    area = insetXY(area, kMargin, 14);
    drawFrontlightControls(f, area, ctx, ctx.selectedFrontlightIndex);
    present(fullRefresh);
}

void drawFrontlightPanel(const AppContext& ctx, int selected) {
    Pass pass;
    Frame& f = pass.frame();
    // Pulled down from the top edge: the full width, as tall as its controls;
    // the screen stays visible below it.
    const int16_t bandH = (int16_t)(lineHeight(FontTitle) + 14);
    const int16_t hintH = touchUi() ? (int16_t)(lineHeight(FontSmall) + 12) : 0;
    const int16_t h = (int16_t)(bandH + 14 + frontlightControlsHeight() + hintH + 14);
    Rect box{0, 0, width(), h};
    fill(Rect{0, h, width(), 6}, Color::Black);   // a shadow along its bottom edge
    fill(box, Color::White);
    f.hit(box, 6, 0);   // a tap on the panel does not close it (one below it does)
    Rect band = takeTop(box, bandH);
    fill(band, Color::Black);
    text(insetX(band, kMargin), tr(MsgId::FRONTLIGHT_TITLE), FontTitle, Color::White);
    Rect area = insetXY(box, kMargin, 14);
    drawFrontlightControls(f, area, ctx, selected);
    if (hintH) {
        takeTop(area, 6);
        text(takeTop(area, (int16_t)(hintH - 6)), tr(MsgId::FRONTLIGHT_PANEL_HINT), FontSmall, Color::Black,
             TextAlign::Center);
    }
    present(false);
}

// ============================================================================
// Incremental updates: with an immediate-mode UI a full repaint is cheap, so
// these redraw the screen and push a fast (partial) refresh.
// ============================================================================

void updateSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSettingsScreen(ctx, false);
}
void updateSettingsItem(AppContext& ctx, int) { drawSettingsScreen(ctx, false); }

void updateWifiSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawWifiSettingsScreen(ctx, false);
}
void updateWifiSettingsItem(AppContext& ctx, int) { drawWifiSettingsScreen(ctx, false); }

void updateWebSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawWebSettingsScreen(ctx, false);
}

void updateServerSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawServerSettingsScreen(ctx, false);
}
void updateServerSettingsItem(AppContext& ctx, int) { drawServerSettingsScreen(ctx, false); }

void updateSyncSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSyncSettingsScreen(ctx, false);
}
void updateSyncSettingsItem(AppContext& ctx, int) { drawSyncSettingsScreen(ctx, false); }

void updateLanguageSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawLanguageSettingsScreen(ctx, false);
}
void updateLanguageSettingsItem(AppContext& ctx, int) { drawLanguageSettingsScreen(ctx, false); }

void updateSleepCoverSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSleepCoverSettingsScreen(ctx, false);
}
void updateSleepCoverSettingsItem(AppContext& ctx, int) { drawSleepCoverSettingsScreen(ctx, false); }

void updateSdSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSdSettingsScreen(ctx, false);
}

void updateUsbMscSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawUsbMscSettingsScreen(ctx, false);
}
void updateUsbMscSettingsItem(AppContext& ctx, int) { drawUsbMscSettingsScreen(ctx, false); }
