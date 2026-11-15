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
#include "../../app/screens/choices.h"

using namespace uikit;

namespace {

// One row of a settings list. `value` goes on the right (short values) or,
// with `under`, as a second line under the title (a description). `toggle`
// 0 / 1 draws a switch, `checked` the mark of the active choice; `open` a
// chevron (the row opens a screen of its own).
struct SettingItemDef {
    const char* title = nullptr;
    String titleStr;             // a title made for the row (used instead of `title` when set)
    String value;
    const char* tag = nullptr;   // a warning label (B4), else unused
    bool under = false;
    int8_t toggle = -1;
    bool checked = false;
    bool open = false;
};

SettingItemDef valueRow(const char* title, const String& value, bool open = true) {
    SettingItemDef d;
    d.title = title;
    d.value = value;
    d.open = open;
    return d;
}
SettingItemDef descRow(const char* title, const String& desc, bool open = false) {
    SettingItemDef d;
    d.title = title;
    d.value = desc;
    d.under = true;
    d.open = open;
    return d;
}
SettingItemDef switchRow(const char* title, bool on, const char* desc = nullptr) {
    SettingItemDef d;
    d.title = title;
    d.toggle = on ? 1 : 0;
    if (desc) {
        d.value = desc;
        d.under = true;
    }
    return d;
}
SettingItemDef choiceRow(const char* title, const char* desc, bool active) {
    SettingItemDef d;
    d.title = title;
    d.value = desc;
    d.under = true;
    d.checked = active;
    return d;
}

struct LangDef {
    AppLanguage lang;
    const char* name;
    const char* sub;
};

using ItemDefFn = SettingItemDef (*)(const AppContext&, int);

// Section captions of the main settings (settingsSectionAt()).
const char* sectionCaption(int sec) {
    switch (sec) {
        case 0:
            return tr(MsgId::SETTINGS_SECTION_READING);
        case 1:
            return tr(MsgId::SETTINGS_SECTION_CONNECT);
        case 2:
            return tr(MsgId::SETTINGS_SECTION_DEVICE);
        default:
            return nullptr;
    }
}

// One settings screen: status line and title, the rows (in sections when
// `sectionOf` is given), a scroll track when they take more than a screen,
// the key tabs. Rows are as tall as their content: one line, or two when a
// description goes under the title.
// What a list screen may draw under its rows (in the room left there).
using BelowFn = void (*)(Frame&, const AppContext&, Rect);

void drawListScreen(const AppContext& ctx, const char* title, ItemDefFn def, int count, int selected,
                    int (*sectionOf)(int), bool fullRefresh, const char* logName, BelowFn below = nullptr) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, title);
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT), tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT)});
    area = insetXY(area, kMargin, 2);

    constexpr int kMax = 24;
    if (count > kMax) count = kMax;
    SettingItemDef items[kMax];
    int16_t heights[kMax];
    for (int i = 0; i < count; i++) {
        items[i] = def(ctx, i);
        const bool two = items[i].under && items[i].value.length() > 0;
        heights[i] = rowHeight(two);
    }
    const int16_t gap = 4;
    auto caption = [&](int i) { return sectionOf ? sectionCaption(sectionOf(i)) : nullptr; };
    auto entryH = [&](int i) { return (int16_t)(heights[i] + gap + (caption(i) ? sectionHeight() : 0)); };

    int16_t belowH = 0;
    if (below) {
        // Keep both bars (including wrapped titles) and one sample line visible
        // in landscape too, while leaving at least one complete settings row.
        const int lh = lineHeight(FontSmall);
        auto barHeight = [&](int mask) {
            return StatusLine::textLines(mask) * lh + ((mask & SB_BOOK_BAR) ? 10 : 0) + ((mask & SB_CH_BAR) ? 10 : 0) +
                   (mask ? 6 : 0);
        };
        const int required = 6 + sectionHeight() + 4 + 16 + barHeight(ctx.config.sbTopItems) +
                             barHeight(ctx.config.sbBottomItems) + lh;
        const int preferred = std::max(required, std::min(270, std::max(150, area.height * 2 / 5)));
        belowH = (int16_t)std::min(preferred, std::max(0, area.height - rowHeight(false) - gap));
    }
    const int16_t listH = (int16_t)(area.height - belowH);

    // Pages: as many rows as fit, from the first; the page the selection is on.
    int first = 0, perPage = count;
    {
        int start = 0;
        while (start < count) {
            int end = start, used = 0;
            while (end < count && (end == start || used + entryH(end) <= listH)) used += entryH(end++);
            if (selected < end || end >= count) {
                first = start;
                perPage = end - start;
                break;
            }
            start = end;
        }
    }
    const bool paged = perPage < count;
    if (paged) setListPager(count, perPage);
    Rect list = Rect{area.x, area.y, (int16_t)(area.width - (paged ? 14 : 0)), listH};
    const int16_t listTop = list.y;
    for (int i = first; i < first + perPage; i++) {
        if (const char* cap = caption(i)) section(takeTop(list, sectionHeight()), cap, i > first);
        const SettingItemDef& it = items[i];
        RowOpts o;
        if (!it.under && it.value.length()) o.value = it.value.c_str();
        o.toggle = it.toggle;
        o.checked = it.checked;
        o.chevron = it.open;
        o.badge = it.tag;
        const char* title = it.titleStr.length() ? it.titleStr.c_str() : it.title;
        row(f, takeTop(list, heights[i]), title, it.under ? it.value.c_str() : nullptr, o, i == selected, 1,
            (int16_t)i);
        takeTop(list, gap);
    }
    if (paged)
        scrollbar(Rect{(int16_t)(area.right() - 6), listTop, 8, (int16_t)(list.y - listTop)}, first, perPage, count);
    if (below) below(f, ctx, Rect{area.x, (int16_t)(area.bottom() - belowH), area.width, belowH});
    present(fullRefresh);
    Serial.printf("[UI] %s finished in %lu ms.\n", logName, millis() - t0);
}

SettingItemDef getSettingItemDef(const AppContext& ctx, int index) {
    if (index == usbMscSettingIndex()) {
        return valueRow(tr(MsgId::SETTINGS_ITEM_USB_MSC),
                        tr(ctx.config.usbMscEnabled ? MsgId::SETTINGS_ON : MsgId::SETTINGS_OFF));
    }
    if (index == touchWakeSettingIndex()) {
        return switchRow(tr(MsgId::SETTINGS_ITEM_TOUCH_WAKE), ctx.config.touchWakeEnabled);
    }
    if (index == webSettingIndex()) {
        return valueRow(tr(MsgId::SETTINGS_ITEM_WEB), "");
    }
    if (index == readingSettingIndex()) {
        return valueRow(tr(MsgId::SETTINGS_ITEM_READING), "");
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
        return valueRow(tr(MsgId::SETTINGS_ITEM_ORIENTATION), val);   // a list of its own
    }
    switch (index) {
        case 0:
            return valueRow(tr(MsgId::SETTINGS_ITEM_LANGUAGE), I18n::getLanguageName(ctx.config.language));
        case 1:
            return valueRow(tr(MsgId::SETTINGS_ITEM_WIFI),
                            ctx.config.wifiSsid.length() > 0 ? ctx.config.wifiSsid : String(tr(MsgId::SETTINGS_OFF)));
        case 2: {
            // The host without its scheme: the row has little room.
            String host = ctx.config.serverHost;
            const int sep = host.indexOf("://");
            if (sep >= 0) host = host.substring(sep + 3);
            return valueRow(tr(MsgId::SETTINGS_ITEM_SERVER), host.length() ? host : String(tr(MsgId::SETTINGS_OFF)));
        }
        case 3:
            return valueRow(tr(MsgId::SETTINGS_ITEM_SLEEP), "");
        case 4:   // reload / save: a submenu (service actions, not settings of their own)
            return valueRow(tr(MsgId::SETTINGS_ITEM_SD_FILE), "");
        case 6: {   // Synchronization submenu: how many of the moments are on
            const int on = (int)ctx.config.syncOnClose + (int)ctx.config.syncOnSleep + (int)ctx.config.syncOnWake +
                           (int)ctx.config.syncOnBoot;
            String v = on ? (String(on) + "/4") : String(tr(MsgId::SETTINGS_OFF));
            return valueRow(tr(MsgId::SETTINGS_ITEM_SYNC), v);
        }
        case 7: {
            char v[16];
            if (ctx.config.frontlight <= 0)
                snprintf(v, sizeof(v), "%s", tr(MsgId::SETTINGS_OFF));
            else
                snprintf(v, sizeof(v), "%d%%", ctx.config.frontlight);
            return valueRow(tr(MsgId::SETTINGS_ITEM_FRONTLIGHT), String(v));
        }
        case 5:
        default:
            return valueRow(tr(MsgId::SETTINGS_ITEM_DEVICE_INFO), deviceDisplayName(), false);
    }
}

// The main list in display order (sections): place -> item.
SettingItemDef getSettingRowDef(const AppContext& ctx, int pos) { return getSettingItemDef(ctx, settingsItemAt(pos)); }

SettingItemDef getWifiSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return descRow(tr(MsgId::WIFI_ITEM_SCAN), tr(MsgId::WIFI_ITEM_SCAN_SUB), true);
        case 1:
            return valueRow(tr(MsgId::WIFI_ITEM_SSID),
                            ctx.config.wifiSsid.length() > 0 ? ctx.config.wifiSsid : tr(MsgId::WIFI_ITEM_SSID_NONE));
        case 2: {
            String maskedWifiPass = "";
            for (size_t p = 0; p < ctx.config.wifiPass.length() && p < 12; p++) maskedWifiPass += "*";
            if (maskedWifiPass.length() == 0) maskedWifiPass = tr(MsgId::WIFI_PASS_OPEN);
            return valueRow(tr(MsgId::WIFI_ITEM_PASS), maskedWifiPass);
        }
        case 3: {
            char testBuf[96];
            if (ctx.config.wifiSsid.length() > 0) {
                snprintf(testBuf, sizeof(testBuf), tr(MsgId::WIFI_ITEM_TEST_SUB_FMT), ctx.config.wifiSsid.c_str());
            } else {
                snprintf(testBuf, sizeof(testBuf), "%s", tr(MsgId::WIFI_ITEM_TEST_NONE));
            }
            return descRow(tr(MsgId::WIFI_ITEM_TEST), String(testBuf));
        }
        case 4:
        default:
            return descRow(tr(MsgId::WIFI_ITEM_FORGET), tr(MsgId::WIFI_ITEM_FORGET_SUB));
    }
}

SettingItemDef getWebSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return descRow(tr(MsgId::WIFI_ITEM_WEB), tr(MsgId::WIFI_ITEM_WEB_SUB), true);
        case 1:
        default: {
            char sub[80];
            if (ctx.config.wifiSsid.length() > 0) {
                snprintf(sub, sizeof(sub), tr(MsgId::WEB_MENU_PORTAL_WIFI_SUB), ctx.config.wifiSsid.c_str());
            } else {
                snprintf(sub, sizeof(sub), "%s", tr(MsgId::WEB_MENU_PORTAL_WIFI_NO_CFG));
            }
            return descRow(tr(MsgId::WEB_MENU_PORTAL_WIFI), String(sub), true);
        }
    }
}

SettingItemDef getServerSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0: {
            SettingItemDef d = descRow(tr(MsgId::SERVER_ITEM_HOST), ctx.config.serverHost.length() > 0
                                                                            ? ctx.config.serverHost
                                                                            : tr(MsgId::SERVER_ITEM_HOST_NONE));
            // B4: an address that is not https:// is flagged
            if (BookOrbitProtocol::isPlainHttp(ctx.config.serverHost.c_str())) d.tag = tr(MsgId::SERVER_TAG_HTTP);
            return d;
        }
        case 1:
            return valueRow(tr(MsgId::SERVER_ITEM_USER), ctx.config.bookOrbitUser.length() > 0
                                                                 ? ctx.config.bookOrbitUser
                                                                 : String(tr(MsgId::SERVER_ITEM_USER_NONE)));
        case 2: {
            String maskedPass = "";
            for (size_t p = 0; p < ctx.config.bookOrbitPass.length() && p < 12; p++) maskedPass += "*";
            if (maskedPass.length() == 0) maskedPass = tr(MsgId::SERVER_PASS_NONE);
            return valueRow(tr(MsgId::SERVER_ITEM_PASS), maskedPass);
        }
        case 3:
        default:
            return descRow(tr(MsgId::SERVER_ITEM_TEST), tr(MsgId::SERVER_ITEM_TEST_SUB));
    }
}

SettingItemDef getSyncSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return switchRow(tr(MsgId::SYNC_ITEM_CLOSE), ctx.config.syncOnClose);
        case 1:
            return switchRow(tr(MsgId::SYNC_ITEM_SLEEP), ctx.config.syncOnSleep);
        case 2:
            return switchRow(tr(MsgId::SYNC_ITEM_WAKE), ctx.config.syncOnWake);
        case 3:
        default:
            return switchRow(tr(MsgId::SYNC_ITEM_BOOT), ctx.config.syncOnBoot);
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
        return descRow(Appearance::label(row), v, true);
    }
    const int on = Appearance::toggleState(ctx.config, row);
    if (on >= 0) return switchRow(Appearance::label(row), on > 0);
    return valueRow(Appearance::label(row), String(Appearance::value(ctx.config, row, buf, sizeof(buf))),
                    row == Appearance::FULL_REFRESH);   // a list of its own
}

SettingItemDef getStatusBarItemDef(const AppContext& ctx, int index) {
    const int on = StatusBarMenu::toggleState(ctx.config, index);
    if (on >= 0) return switchRow(StatusBarMenu::label(index), on > 0);
    char buf[64];
    const bool list = index == StatusBarMenu::TOP || index == StatusBarMenu::BOTTOM;   // own screens
    return valueRow(StatusBarMenu::label(index), String(StatusBarMenu::value(ctx.config, index, buf, sizeof(buf))),
                    list);
}

SettingItemDef getSleepSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0: {
            String sleepVal = !ctx.config.coverOnSleep
                                      ? String(tr(MsgId::SLEEP_COVER_MODE_OFF))
                                      : (ctx.config.sleepCoverFitMode == 0 ? String(tr(MsgId::SLEEP_COVER_MODE_FIT))
                                                                           : String(tr(MsgId::SLEEP_COVER_MODE_FILL)));
            return valueRow(tr(MsgId::SETTINGS_ITEM_SLEEP_COVER), sleepVal);
        }
        case 1: {
            char buf[32];
            Choices::label(Choices::SLEEP_HOLD, Choices::active(ctx.config, Choices::SLEEP_HOLD), buf, sizeof(buf));
            return valueRow(tr(MsgId::SETTINGS_ITEM_SLEEP_HOLD), String(buf));
        }
        case 2:
        default: {
            char buf[32];
            Choices::label(Choices::WAKE_HOLD, Choices::active(ctx.config, Choices::WAKE_HOLD), buf, sizeof(buf));
            return valueRow(tr(MsgId::SETTINGS_ITEM_WAKE_HOLD), String(buf));
        }
    }
}

SettingItemDef getSleepCoverSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0: {
            const bool active = ctx.config.coverOnSleep && (ctx.config.sleepCoverFitMode == 0);
            return choiceRow(tr(MsgId::SLEEP_COVER_MODE_FIT), tr(MsgId::SLEEP_COVER_FIT_SUB), active);
        }
        case 1: {
            const bool active = ctx.config.coverOnSleep && (ctx.config.sleepCoverFitMode == 1);
            return choiceRow(tr(MsgId::SLEEP_COVER_MODE_FILL), tr(MsgId::SLEEP_COVER_FILL_SUB), active);
        }
        case 2:
        default: {
            const bool active = !ctx.config.coverOnSleep;
            return choiceRow(tr(MsgId::SLEEP_COVER_MODE_OFF), tr(MsgId::SLEEP_COVER_OFF_SUB), active);
        }
    }
}

SettingItemDef getOrientationItemDef(const AppContext& ctx, int index) {
    static const MsgId k[] = {MsgId::APPEAR_ORIENT_PORTRAIT, MsgId::APPEAR_ORIENT_LANDSCAPE,
                              MsgId::APPEAR_ORIENT_PORTRAIT_FLIPPED, MsgId::APPEAR_ORIENT_LANDSCAPE_FLIPPED};
    const bool imu = BoardImu.present();
    const bool active = index == orientationChoiceActive(ctx.config);
    if (imu && index == 0) return choiceRow(tr(MsgId::SETTINGS_ORIENT_AUTO), "", active);
    return choiceRow(tr(k[(index - (imu ? 1 : 0)) & 3]), "", active);
}

SettingItemDef getChoiceItemDef(const AppContext& ctx, int index) {
    char buf[48];
    Choices::label(ctx.choiceKind, index, buf, sizeof(buf));
    SettingItemDef d = choiceRow("", "", index == Choices::active(ctx.config, ctx.choiceKind));
    d.titleStr = buf;
    return d;
}

SettingItemDef getSdSettingItemDef(const AppContext&, int index) {
    if (index == 0) return descRow(tr(MsgId::SETTINGS_ITEM_RELOAD_SD), tr(MsgId::SETTINGS_ITEM_RELOAD_SD_SUB));
    return descRow(tr(MsgId::SETTINGS_ITEM_SAVE_SD), tr(MsgId::SETTINGS_ITEM_SAVE_SD_SUB));
}

SettingItemDef getUsbMscSettingItemDef(const AppContext& ctx, int index) {
    if (index == 0) {
        return descRow(tr(MsgId::USB_MSC_ITEM_CONNECT),
                       ctx.config.usbMscEnabled ? tr(MsgId::USB_MSC_CONNECT_SUB_READY)
                                                : tr(MsgId::USB_MSC_CONNECT_SUB_OFF),
                       ctx.config.usbMscEnabled);
    }
    return switchRow(tr(MsgId::USB_MSC_ITEM_TOGGLE), ctx.config.usbMscEnabled);
}

const LangDef LANG_DEFS[(size_t)AppLanguage::COUNT] = {{AppLanguage::EN, "English", "English (Default)"},
                                                       {AppLanguage::RU, "Русский", "Русский язык"},
                                                       {AppLanguage::ES, "Español", "Idioma español"}};

SettingItemDef getLanguageItemDef(const AppContext& ctx, int i) {
    const bool active = (ctx.config.language == LANG_DEFS[i].lang);
    return choiceRow(LANG_DEFS[i].name, LANG_DEFS[i].sub, active);
}

}  // namespace

// ============================================================================
// Full screens
// ============================================================================

void drawSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SETTINGS_TITLE), getSettingRowDef, settingsItemCount(), ctx.selectedSettingIndex,
                   settingsSectionAt, fullRefresh, "drawSettingsScreen");
}

void drawWifiSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::WIFI_TITLE), getWifiSettingItemDef, NUM_WIFI_SETTINGS_ITEMS,
                   ctx.selectedWifiSettingIndex, nullptr, fullRefresh, "drawWifiSettingsScreen");
}

void drawWebSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::WEB_MENU_TITLE), getWebSettingItemDef, NUM_WEB_SETTINGS_ITEMS,
                   ctx.selectedWebSettingIndex, nullptr, fullRefresh, "drawWebSettingsScreen");
}

void drawServerSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SERVER_TITLE), getServerSettingItemDef, NUM_SERVER_SETTINGS_ITEMS,
                   ctx.selectedServerSettingIndex, nullptr, fullRefresh, "drawServerSettingsScreen");
}

void drawSyncSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SYNC_TITLE), getSyncSettingItemDef, NUM_SYNC_SETTINGS_ITEMS,
                   ctx.selectedSyncSettingIndex, nullptr, fullRefresh, "drawSyncSettingsScreen");
}

void drawReadingSettingsScreen(AppContext& ctx, bool fullRefresh) {
    int& sel = ctx.selectedReadingSettingIndex;
    const int n = Appearance::Reading::count();
    if (sel < 0 || sel >= n) sel = 0;
    drawListScreen(ctx, tr(MsgId::READING_TITLE), getReadingSettingItemDef, n, sel, nullptr, fullRefresh,
                   "drawReadingSettingsScreen");
}

void drawStatusBarScreen(AppContext& ctx, bool fullRefresh) {
    int& sel = ctx.selectedStatusBarIndex;
    if (sel < 0 || sel >= StatusBarMenu::COUNT) sel = 0;
    drawListScreen(ctx, tr(MsgId::SB_SCREEN_TITLE), getStatusBarItemDef, StatusBarMenu::COUNT, sel, nullptr,
                   fullRefresh, "drawStatusBarScreen");
}

void drawSdSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SD_SETTINGS_TITLE), getSdSettingItemDef, NUM_SD_SETTINGS_ITEMS,
                   ctx.selectedSdSettingIndex, nullptr, fullRefresh, "drawSdSettingsScreen");
}

void drawUsbMscSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::USB_MSC_MENU_TITLE), getUsbMscSettingItemDef, NUM_USB_MSC_ITEMS,
                   ctx.selectedUsbMscIndex, nullptr, fullRefresh, "drawUsbMscSettingsScreen");
}

void drawUsbMscScreen(const AppContext& ctx, bool fullRefresh) {
    (void)ctx;
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::USB_MSC_SCREEN_TITLE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_DONE), tr(MsgId::KEY_BACK), tr(MsgId::KEY_DONE)});
    area = insetXY(area, kMargin, 16);

    const char* statusStr = tr(MsgId::USB_MSC_STATE_WAIT);
    switch (UsbMsc.state()) {
        case freeink::UsbMassStorageState::Connected:
            statusStr = tr(MsgId::USB_MSC_STATE_CONN);
            break;
        case freeink::UsbMassStorageState::Accessed:
            statusStr = tr(MsgId::USB_MSC_STATE_ACCESS);
            break;
        case freeink::UsbMassStorageState::Ejected:
            statusStr = tr(MsgId::USB_MSC_STATE_EJECT);
            break;
        case freeink::UsbMassStorageState::Disconnected:
            statusStr = tr(MsgId::USB_MSC_STATE_DISC);
            break;
        case freeink::UsbMassStorageState::IoError:
            statusStr = tr(MsgId::USB_MSC_STATE_ERR);
            break;
        case freeink::UsbMassStorageState::Idle:
        default:
            statusStr = tr(MsgId::USB_MSC_STATE_WAIT);
            break;
    }

    const int16_t boxH = (int16_t)(lineHeight(FontTitle) + 32);
    Rect box = takeTop(area, boxH);
    frameRect(box, Color::Black, 2, kRadius);
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
        fill(btn, Color::Black, kRadius);
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
        frameRect(boxArea, Color::Black, 1, kRadius);
        text(insetXY(boxArea, 12, 4), exitHint, FontBody, Color::Black, TextAlign::Center, 2);
    }

    present(fullRefresh);
}

void drawLanguageSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::LANG_SCREEN_TITLE), getLanguageItemDef, (int)AppLanguage::COUNT,
                   ctx.selectedLanguageIndex, nullptr, fullRefresh, "drawLanguageSettingsScreen");
}

void drawOrientationSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SETTINGS_ITEM_ORIENTATION), getOrientationItemDef, orientationChoiceCount(),
                   ctx.selectedOrientationIndex, nullptr, fullRefresh, "drawOrientationSettingsScreen");
}

void updateOrientationSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawOrientationSettingsScreen(ctx, false);
}

namespace {

// The status bar settings as they will look: a small page with its top and
// bottom lines, the row under the cursor tried out (before it is chosen).
void statusBarPreview(Frame& f, const AppContext& ctx, Rect area) {
    (void)f;
    const int16_t capH = sectionHeight();
    if (area.height < capH + 60) return;
    // Little room (a long list on a small screen): the bottom line alone.
    const bool compact = area.height < capH + 140;
    takeTop(area, 6);
    section(takeTop(area, capH), tr(MsgId::SB_PREVIEW));
    Rect page = takeTop(area, (int16_t)(area.height - 4));
    frameRect(page, Color::Black, 1, kRadius);
    Rect in = insetXY(page, 14, 8);

    AppConfig cfg = ctx.config;
    // On a list of values the row under the cursor is tried out.
    if (ctx.currentState == STATE_CHOICE) Choices::apply(cfg, ctx.choiceKind, ctx.selectedChoiceIndex);
    const char* chapter = tr(MsgId::SB_PREVIEW_CHAPTER);

    // Top line: progress bars, then sides and wrapped center line.
    const int topItems = cfg.sbTopItems;
    bool anyTop = false;
    if (topItems & SB_BOOK_BAR) {
        static const uint16_t kTicks[] = {30, 95, 170, 260, 330, 420, 510, 590, 680, 760, 850, 930};
        bookBar(takeTop(in, 4), 40, kTicks, (int)(sizeof(kTicks) / sizeof(kTicks[0])));
        takeTop(in, 6);
        anyTop = true;
    }
    if (topItems & SB_CH_BAR) {
        chapterBar(takeTop(in, 4), 3, 18);
        takeTop(in, 6);
        anyTop = true;
    }
    if (StatusLine::textLines(topItems)) {
        Rect row = takeTop(in, lineHeight(FontSmall));
        const StatusLine::Where w{2, 12, 3, 18, 12, 340, 17, 4};
        char clk[8] = "";
        if (topItems & SB_CLOCK) {
            snprintf(clk, sizeof(clk), "12:00");
        }
        char l[48], r[48], c[160];
        StatusLine::sides(topItems, w, clk, l, sizeof(l), r, sizeof(r));
        const char* bookTitle = tr(MsgId::SB_PREVIEW_TITLE);
        StatusLine::center(topItems, w, bookTitle, chapter, c, sizeof(c));
        const int16_t lw = *l ? (int16_t)(textWidth(FontSmall, l) + 8) : 0;
        int16_t rw = 0;
        if (topItems & SB_BATTERY) {
            batteryGlyph((int16_t)(row.right() - 27), (int16_t)(row.y + (row.height - 13) / 2), Color::Black);
            rw = (int16_t)(27 + (*r ? 10 : 0));
        }
        if (*r) {
            const int16_t tw = (int16_t)(textWidth(FontSmall, r) + 4);
            text(Rect{(int16_t)(row.right() - rw - tw), row.y, tw, row.height}, r, FontSmall, Color::Black,
                 TextAlign::Right);
            rw = (int16_t)(rw + tw);
        }
        if (*l) text(Rect{row.x, row.y, lw, row.height}, l, FontSmall);
        if (*c) {
            Rect mid;
            if (StatusLine::centerAbove(topItems)) {
                mid = takeTop(in, lineHeight(FontSmall));
            } else {
                mid = Rect{(int16_t)(row.x + lw), row.y, (int16_t)(row.width - lw - rw), row.height};
            }
            if (mid.width > 20) text(mid, c, FontSmall, Color::Black, TextAlign::Center);
        }
        anyTop = true;
    }
    if (anyTop) takeTop(in, 6);
    // Bottom: page in the chapter, chapter, percent and a thin line; the line
    // alone; nothing.
    const int items = cfg.sbBottomItems;
    bool any = false;
    if (items & SB_BOTTOM_BOOK_BAR) {   // solid, a notch at each chapter
        static const uint16_t kTicks[] = {30, 95, 170, 260, 330, 420, 510, 590, 680, 760, 850, 930};
        bookBar(takeBottom(in, 4), 40, kTicks, (int)(sizeof(kTicks) / sizeof(kTicks[0])));
        takeBottom(in, 6);
        any = true;
    }
    if (items & SB_BOTTOM_CH_BAR) {   // solid: 3 of 18 pages read
        chapterBar(takeBottom(in, 4), 3, 18);
        takeBottom(in, 6);
        any = true;
    }
    if (StatusLine::textLines(items)) {
        // The same parts as on a page (core/status_line.h), with sample numbers.
        Rect row = takeBottom(in, lineHeight(FontSmall));
        const StatusLine::Where w{2, 12, 3, 18, 12, 340, 17, 4};
        char clk[8] = "";
        if (items & SB_BOTTOM_CLOCK) {
            snprintf(clk, sizeof(clk), "12:00");
        }
        char l[48], r[48], c[160];
        StatusLine::sides(items, w, clk, l, sizeof(l), r, sizeof(r));
        const char* bookTitle = tr(MsgId::SB_PREVIEW_TITLE);
        StatusLine::center(items, w, bookTitle, chapter, c, sizeof(c));
        const int16_t lw = *l ? (int16_t)(textWidth(FontSmall, l) + 8) : 0;
        int16_t rw = 0;
        if (items & SB_BOTTOM_BATTERY) {
            batteryGlyph((int16_t)(row.right() - 27), (int16_t)(row.y + (row.height - 13) / 2), Color::Black);
            rw = (int16_t)(27 + (*r ? 10 : 0));
        }
        if (*r) {
            const int16_t tw = (int16_t)(textWidth(FontSmall, r) + 4);
            text(Rect{(int16_t)(row.right() - rw - tw), row.y, tw, row.height}, r, FontSmall, Color::Black,
                 TextAlign::Right);
            rw = (int16_t)(rw + tw);
        }
        if (*l) text(Rect{row.x, row.y, lw, row.height}, l, FontSmall);
        if (*c) {
            Rect mid;
            if (StatusLine::centerAbove(items)) {
                mid = takeBottom(in, lineHeight(FontSmall));
            } else {
                mid = Rect{(int16_t)(row.x + lw), row.y, (int16_t)(row.width - lw - rw), row.height};
            }
            if (mid.width > 20) text(mid, c, FontSmall, Color::Black, TextAlign::Center);
        }
        any = true;
    }
    if (any) takeBottom(in, 6);
    if (compact) return;
    // The text between them, as many lines as the room leaves (never overlapping the bottom bar).
    const int16_t lh = lineHeight(FontSmall);
    const int16_t fitLines = (int16_t)(in.height / lh);
    if (fitLines > 0) {
        const uint8_t lines = (uint8_t)std::min<int>(fitLines, 10);
        text(Rect{in.x, in.y, in.width, (int16_t)(lines * lh)}, tr(MsgId::APPEAR_SAMPLE), FontSmall, Color::Black,
             TextAlign::Left, lines);
    }
}

}  // namespace

int sbItemAt(int index) {
    static const int kItems[NUM_SB_ITEMS] = {SB_CH_PAGES,     SB_CH_PERCENT, SB_CH_BAR,   SB_BOOK_PAGES,
                                             SB_BOOK_PERCENT, SB_BOOK_BAR,   SB_CH_TITLE, SB_CH_NUMBER,
                                             SB_BOOK_TITLE,   SB_BATTERY,    SB_CLOCK};
    return index >= 0 && index < NUM_SB_ITEMS ? kItems[index] : 0;
}

int sbTopItemAt(int index) { return sbItemAt(index); }
int sbBottomItemAt(int index) { return sbItemAt(index); }

namespace {
SettingItemDef getSbTopItemDef(const AppContext& ctx, int index) {
    static const MsgId kLabels[NUM_SB_ITEMS] = {MsgId::SBB_CH_PAGES,   MsgId::SBB_CH_PERCENT,   MsgId::SBB_CH_BAR,
                                                MsgId::SBB_BOOK_PAGES, MsgId::SBB_BOOK_PERCENT, MsgId::SBB_BOOK_BAR,
                                                MsgId::SBB_CH_TITLE,   MsgId::SBB_CH_NUMBER,    MsgId::SB_TOP_BOOK,
                                                MsgId::SB_BATTERY,     MsgId::SB_CLOCK};
    return switchRow(tr(kLabels[index]), (ctx.config.sbTopItems & sbItemAt(index)) != 0);
}
}  // namespace

void drawSbTopScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, StatusBarMenu::label(StatusBarMenu::TOP), getSbTopItemDef, NUM_SB_TOP_ITEMS,
                   ctx.selectedSbTopIndex, nullptr, fullRefresh, "drawSbTopScreen", statusBarPreview);
}

void updateSbTopSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSbTopScreen(ctx, false);
}

namespace {
SettingItemDef getSbBottomItemDef(const AppContext& ctx, int index) {
    static const MsgId kLabels[NUM_SB_ITEMS] = {MsgId::SBB_CH_PAGES,   MsgId::SBB_CH_PERCENT,   MsgId::SBB_CH_BAR,
                                                MsgId::SBB_BOOK_PAGES, MsgId::SBB_BOOK_PERCENT, MsgId::SBB_BOOK_BAR,
                                                MsgId::SBB_CH_TITLE,   MsgId::SBB_CH_NUMBER,    MsgId::SB_TOP_BOOK,
                                                MsgId::SB_BATTERY,     MsgId::SB_CLOCK};
    return switchRow(tr(kLabels[index]), (ctx.config.sbBottomItems & sbItemAt(index)) != 0);
}
}  // namespace

void drawSbBottomScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, StatusBarMenu::label(StatusBarMenu::BOTTOM), getSbBottomItemDef, NUM_SB_BOTTOM_ITEMS,
                   ctx.selectedSbBottomIndex, nullptr, fullRefresh, "drawSbBottomScreen", statusBarPreview);
}

void updateSbBottomSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSbBottomScreen(ctx, false);
}

void drawChoiceScreen(AppContext& ctx, bool fullRefresh) {
    const int kind = ctx.choiceKind;
    drawListScreen(ctx, Choices::title(kind), getChoiceItemDef, Choices::count(kind), ctx.selectedChoiceIndex, nullptr,
                   fullRefresh, "drawChoiceScreen", nullptr);
}

void updateChoiceSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawChoiceScreen(ctx, false);
}

void drawSleepSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SLEEP_SETTINGS_TITLE), getSleepSettingItemDef, NUM_SLEEP_SETTINGS_ITEMS,
                   ctx.selectedSleepSettingIndex, nullptr, fullRefresh, "drawSleepSettingsScreen");
}

void updateSleepSettingsSelection(AppContext& ctx, int prevIndex, int newIndex) {
    if (prevIndex != newIndex) drawSleepSettingsScreen(ctx, false);
}

void updateSleepSettingsItem(AppContext& ctx, int) { drawSleepSettingsScreen(ctx, false); }

void drawSleepCoverSettingsScreen(AppContext& ctx, bool fullRefresh) {
    drawListScreen(ctx, tr(MsgId::SLEEP_COVER_TITLE), getSleepCoverSettingItemDef, NUM_SLEEP_COVER_ITEMS,
                   ctx.selectedSleepCoverIndex, nullptr, fullRefresh, "drawSleepCoverSettingsScreen");
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
    const MsgId labels[2] = {MsgId::FRONTLIGHT_BRIGHTNESS, MsgId::FRONTLIGHT_WARMTH};
    const int values[2] = {ctx.config.frontlight, ctx.config.frontlightWarm};
    const int16_t th = lineHeight(FontTitle);
    s_tracks[0] = s_tracks[1] = Rect{};
    for (int r = 0; r < rows; r++) {
        const bool sel = (r == selected);
        const Color ink = Color::Black;
        Rect row = takeTop(area, (int16_t)(th + 74));
        takeTop(area, 12);
        if (sel)
            selectionFrame(row);
        else
            frameRect(row, Color::Black, 1, kRadius);
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
        for (const Rect& b : {minus, plus}) frameRect(b, Color::Black, 2, kRadiusSmall);
        const Color btnInk = Color::Black;
        text(minus, "-", FontLarge, btnInk, TextAlign::Center);
        text(plus, "+", FontLarge, btnInk, TextAlign::Center);
        Rect track{(int16_t)(minus.right() + 14), (int16_t)(ctl.y + ctl.height / 2 - 6),
                   (int16_t)(plus.x - minus.right() - 28), 12};
        progress(f, track, values[r] * 10);
        s_tracks[r] = track;
        f.hit(minus, 2, (int16_t)r);
        f.hit(plus, 3, (int16_t)r);
        f.hit(Rect{track.x, ctl.y, track.width, ctl.height}, 4, (int16_t)r);
    }
    // What a press does: off -> "Turn on" (filled), on -> "Turn off" (OK on the keys).
    const bool isOn = ctx.config.frontlight > 0;
    Rect btn = takeTop(area, (int16_t)(lineHeight(FontBody) + 24));
    if (!isOn)
        fill(btn, Color::Black, kRadius);
    else
        frameRect(btn, Color::Black, 2, kRadius);
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
    hline(0, width(), h, Color::Black, 2);   // the panel's lower edge
    fill(box, Color::White);
    f.hit(box, 6, 0);   // a tap on the panel does not close it (one below it does)
    Rect band = takeTop(box, bandH);
    text(insetX(band, kMargin), tr(MsgId::FRONTLIGHT_TITLE), FontTitle, Color::Black, TextAlign::Center);
    hline(kMargin, (int16_t)(width() - kMargin), (int16_t)(band.bottom() - 1));
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
