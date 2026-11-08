#include "settings_view.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../hal/frontlight.h"

using namespace uikit;

namespace {

struct SettingItemDef {
    const char* title;
    String value;
    const char* tag;
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
    area = footerHints(f, area, hintNav, hintBack);
    area = insetXY(area, kMargin, 10);
    int16_t cardH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 22);
    int16_t gap = kGap;
    // Long lists (the main settings with a frontlight, touch hints taking more
    // room) shrink the cards' padding so every item stays on screen.
    const int16_t minCardH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 6);
    if (count > 0 && count * (cardH + gap) > area.height) {
        gap = 4;
        cardH = (int16_t)(area.height / count - gap);
        if (cardH < minCardH) cardH = minCardH;
    }
    for (int i = 0; i < count; i++) {
        SettingItemDef item = def(ctx, i);
        card(f, takeTop(area, cardH), item.title, item.value.c_str(), item.tag, i == selected, 1, (int16_t)i);
        takeTop(area, gap);
    }
    present(fullRefresh);
    Serial.printf("[UI] %s finished in %lu ms.\n", logName, millis() - t0);
}

SettingItemDef getSettingItemDef(const AppContext& ctx, int index) {
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
        case 4:
            return { tr(MsgId::SETTINGS_ITEM_RELOAD_SD), tr(MsgId::SETTINGS_ITEM_RELOAD_SD_SUB), tr(MsgId::SETTINGS_TAG_SD) };
        case 5:
            return { tr(MsgId::SETTINGS_ITEM_SAVE_SD), tr(MsgId::SETTINGS_ITEM_SAVE_SD_SUB), tr(MsgId::SETTINGS_TAG_SD) };
        case 7: {   // Synchronization submenu: what is on, in a few words
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
        case 8: {
            char v[48];
            if (ctx.config.frontlight <= 0) snprintf(v, sizeof(v), "%s", tr(MsgId::SETTINGS_FRONTLIGHT_OFF));
            else snprintf(v, sizeof(v), tr(MsgId::SETTINGS_FRONTLIGHT_FMT), ctx.config.frontlight, ctx.config.frontlightWarm);
            return { tr(MsgId::SETTINGS_ITEM_FRONTLIGHT), String(v), tr(MsgId::SETTINGS_TAG_LIGHT) };
        }
        case 6:
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
            return { tr(MsgId::WIFI_ITEM_WEB), tr(MsgId::WIFI_ITEM_WEB_SUB), tr(MsgId::WIFI_TAG_WEB) };
        case 5:
        default:
            return { tr(MsgId::WIFI_ITEM_FORGET), tr(MsgId::WIFI_ITEM_FORGET_SUB), tr(MsgId::WIFI_TAG_RESET) };
    }
}

SettingItemDef getServerSettingItemDef(const AppContext& ctx, int index) {
    switch (index) {
        case 0:
            return { tr(MsgId::SERVER_ITEM_HOST), ctx.config.serverHost.length() > 0 ? ctx.config.serverHost : tr(MsgId::SERVER_ITEM_HOST_NONE), tr(MsgId::SERVER_TAG_URL) };
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

void drawFrontlightScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::FRONTLIGHT_TITLE));
    area = footerHints(f, area, tr(MsgId::FRONTLIGHT_HINT_NAV), tr(MsgId::FRONTLIGHT_HINT_BACK));
    area = insetXY(area, kMargin, 14);

    const int rows = Frontlight.hasWarmth() ? 2 : 1;
    const MsgId labels[2] = { MsgId::FRONTLIGHT_BRIGHTNESS, MsgId::FRONTLIGHT_WARMTH };
    const int values[2] = { ctx.config.frontlight, ctx.config.frontlightWarm };
    s_tracks[0] = s_tracks[1] = Rect{};
    for (int r = 0; r < rows; r++) {
        const bool sel = (r == ctx.selectedFrontlightIndex);
        const Color ink = sel ? Color::White : Color::Black;
        const int16_t th = lineHeight(FontTitle);
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
        for (const Rect& b : { minus, plus }) {
            fill(b, sel ? Color::White : Color::Black, 4);
        }
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
    present(fullRefresh);
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
