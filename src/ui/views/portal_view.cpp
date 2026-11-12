#include "portal_view.h"

#include <algorithm>
#include <cstdio>

#include "../../platform/hal/buttons.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../qr_code.h"

namespace {

using namespace uikit;

constexpr freeink::ui::ActionId kActionNav = 1;

struct PortalPageContent {
    int stepNo;
    const char* title;
    const char* qrPayload;
    const char* primaryLine;
    bool primaryBold;
    const char* secondaryLine;
    bool secondaryBold;
    const char* noteLine;
    const char* touchBtnLabel;
    int targetPage;
};

void drawQrCode(Rect box, const char* payload) {
    fill(box, Color::White);
    frameRect(box, Color::Black, 1);

    qr::QrCode code;
    if (!code.encode(payload)) return;

    const int n = code.size();
    const int side = std::min(box.width, box.height);
    // Reserve at least 3 modules of white quiet zone on every side inside the frame.
    const int mod = (side - 4) / (n + 6);
    if (mod < 1) return;

    const int qrPx = n * mod;
    const int16_t ox = (int16_t)(box.x + (box.width - qrPx) / 2);
    const int16_t oy = (int16_t)(box.y + (box.height - qrPx) / 2);

    for (int y = 0; y < n; ++y) {
        int x = 0;
        while (x < n) {
            if (!code.module(x, y)) {
                ++x;
                continue;
            }
            int run = 1;
            while (x + run < n && code.module(x + run, y)) ++run;
            fill(Rect{(int16_t)(ox + x * mod), (int16_t)(oy + y * mod),
                      (int16_t)(run * mod), (int16_t)mod}, Color::Black);
            x += run;
        }
    }
}

void drawHeaderBand(Rect& area, int stepNo, TextAlign titleAlign, const char* title) {
    const int16_t badgeH = (int16_t)(lineHeight(FontSmall) + 8);
    Rect row = takeTop(area, badgeH);

    char badge[24];
    snprintf(badge, sizeof(badge), tr(MsgId::WEB_STEP_BADGE_FMT), stepNo);
    const int16_t bw = std::min<int16_t>((int16_t)(row.width * 2 / 3), (int16_t)(textWidth(FontSmall, badge) + 20));
    Rect pillRect{row.x, row.y, bw, row.height};
    fill(pillRect, Color::Black, 4);
    text(pillRect, badge, FontSmall, Color::White, TextAlign::Center);

    char counter[12];
    snprintf(counter, sizeof(counter), "%d / 2", stepNo);
    const int16_t cw = (int16_t)(row.width - bw - 8);
    if (cw > 0) {
        text(Rect{(int16_t)(row.x + bw + 8), row.y, cw, row.height}, counter,
             FontSmall, Color::Black, TextAlign::Right);
    }

    takeTop(area, 8);
    if (title && *title) {
        const fi::FontId tf = wrappedHeight(title, FontTitle, area.width, 2) <= lineHeight(FontTitle) * 2
                                  ? FontTitle
                                  : FontBody;
        const int16_t th = wrappedHeight(title, tf, area.width, 2);
        text(takeTop(area, th), title, tf, Color::Black, titleAlign, 2);
    }
}

void drawNavButton(Frame& f, Rect btn, const char* label, int targetPage) {
    fill(btn, Color::Black, 4);
    text(btn, label, FontBody, Color::White, TextAlign::Center);
    f.hit(btn, kActionNav, (int16_t)targetPage);
}

void drawPortraitPage(Frame& f, Rect in, const PortalPageContent& c) {
    drawHeaderBand(in, c.stepNo, TextAlign::Center, c.title);
    takeTop(in, 8);

    if (touchUi()) {
        const int16_t btnH = (int16_t)(lineHeight(FontBody) + 20);
        Rect btn = takeBottom(in, btnH);
        drawNavButton(f, btn, c.touchBtnLabel, c.targetPage);
        takeBottom(in, 10);
    }

    auto takeCenteredBottom = [&](const char* str, fi::FontId font, uint8_t maxLines, int16_t gapAfter) {
        if (!str || !*str) return;
        const int16_t h = wrappedHeight(str, font, in.width, maxLines);
        text(takeBottom(in, h), str, font, Color::Black, TextAlign::Center, maxLines);
        takeBottom(in, gapAfter);
    };

    takeCenteredBottom(c.noteLine, FontSmall, 2, 6);
    takeCenteredBottom(c.secondaryLine, c.secondaryBold ? FontTitle : FontBody, 2, 4);
    takeCenteredBottom(c.primaryLine, c.primaryBold ? FontTitle : FontBody, 2, 10);

    hline(in.x, in.right(), in.bottom());
    takeBottom(in, 10);

    const int16_t qrSide = std::min<int16_t>(380, std::min<int16_t>(in.width, in.height));
    if (qrSide > 20) {
        Rect qrBox{(int16_t)(in.x + (in.width - qrSide) / 2),
                   (int16_t)(in.y + (in.height - qrSide) / 2),
                   qrSide, qrSide};
        drawQrCode(qrBox, c.qrPayload);
    }
}

void drawLandscapePage(Frame& f, Rect in, const PortalPageContent& c) {
    const int16_t gap = 18;
    const int16_t qrSide = std::min<int16_t>(in.height, (int16_t)(in.width * 48 / 100));
    Rect qrBox{in.x, (int16_t)(in.y + (in.height - qrSide) / 2), qrSide, qrSide};
    drawQrCode(qrBox, c.qrPayload);

    Rect col{(int16_t)(qrBox.right() + gap), in.y, (int16_t)(in.width - qrSide - gap), in.height};
    drawHeaderBand(col, c.stepNo, TextAlign::Left, c.title);
    takeTop(col, 8);
    hline(col.x, col.right(), col.y);
    takeTop(col, 10);

    if (touchUi()) {
        const int16_t btnH = (int16_t)(lineHeight(FontBody) + 18);
        Rect btn = takeBottom(col, btnH);
        drawNavButton(f, btn, c.touchBtnLabel, c.targetPage);
        takeBottom(col, 8);
    }

    auto takeLeftTop = [&](const char* str, fi::FontId font, uint8_t maxLines, int16_t gapAfter) {
        if (!str || !*str || col.height <= 0) return;
        const int16_t h = std::min(col.height, wrappedHeight(str, font, col.width, maxLines));
        text(takeTop(col, h), str, font, Color::Black, TextAlign::Left, maxLines);
        takeTop(col, gapAfter);
    };

    takeLeftTop(c.primaryLine, c.primaryBold ? FontTitle : FontBody, 2, 6);
    takeLeftTop(c.secondaryLine, c.secondaryBold ? FontTitle : FontBody, 2, 8);
    takeLeftTop(c.noteLine, FontSmall, 3, 0);
}

}  // namespace

void drawPortalScreen(const char* apSsid, const char* apPass, const char* url, int page) {
    page = (page == 1) ? 1 : 0;

    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::WEB_TITLE));

    KeyLabels keys;
    keys.back = tr(MsgId::KEY_DONE);
    keys.left = (page == 1) ? tr(MsgId::WEB_KEY_WIFI) : "";
    keys.right = (page == 0) ? tr(MsgId::WEB_KEY_URL) : "";
    keys.touch = tr(MsgId::WEB_TOUCH_HINT);
    area = keyHints(f, area, keys);

    area = insetXY(area, kMargin, 10);
    frameRect(area, Color::Black, 2);
    Rect in = insetXY(area, 14, 12);

    const char* ssid = (apSsid && *apSsid) ? apSsid : "BookOrbit-Config";
    const char* passStr = apPass ? apPass : "";
    const char* urlStr = (url && *url) ? url : "http://192.168.4.1/";

    String wifiPayload = qr::formatWifiPayload(ssid, passStr);

    char ssidLine[96];
    snprintf(ssidLine, sizeof(ssidLine), "SSID: %s", ssid);
    char passLine[64];
    snprintf(passLine, sizeof(passLine), tr(MsgId::WEB_STEP1_PASS_FMT), passStr);

    // Display a clean URL without a trailing slash when it's just the root path.
    String urlDisplay = urlStr;
    if (urlDisplay.endsWith("/")) urlDisplay.remove(urlDisplay.length() - 1);

    PortalPageContent content = (page == 0)
        ? PortalPageContent{
              1,
              tr(MsgId::WEB_STEP1_TITLE),
              wifiPayload.c_str(),
              ssidLine,
              false,
              passLine,
              true,
              tr(MsgId::WEB_STEP1_HINT),
              tr(MsgId::WEB_BTN_TO_URL),
              1,
          }
        : PortalPageContent{
              2,
              tr(MsgId::WEB_STEP2_TITLE),
              urlStr,
              urlDisplay.c_str(),
              true,
              tr(MsgId::WEB_STEP3_TITLE),
              false,
              tr(MsgId::WEB_STEP3_DESC),
              tr(MsgId::WEB_BTN_TO_WIFI),
              0,
          };

    if (width() > height()) {
        drawLandscapePage(f, in, content);
    } else {
        drawPortraitPage(f, in, content);
    }

    present(true);
}

PortalAction handlePortalEvent(int btnId, int btnAction, int16_t x, int16_t y, int& page) {
    if (btnAction != ACTION_CLICK) return PortalAction::Stay;
    page = (page == 1) ? 1 : 0;

    switch (btnId) {
        case BTN_BACK:
            return PortalAction::Close;
        case BTN_RIGHT:
        case BTN_DOWN:
        case BTN_SWIPE_LEFT:
            if (page == 0) {
                page = 1;
                return PortalAction::Redraw;
            }
            return PortalAction::Stay;
        case BTN_LEFT:
        case BTN_UP:
            if (page == 1) {
                page = 0;
                return PortalAction::Redraw;
            }
            return PortalAction::Stay;
        case BTN_SWIPE_RIGHT:
            if (page == 1) {
                page = 0;
                return PortalAction::Redraw;
            }
            return PortalAction::Close;
        case BTN_CONFIRM:
            page = 1 - page;
            return PortalAction::Redraw;
        case BTN_TAP: {
            if (backTapped(x, y)) return PortalAction::Close;
            freeink::ui::ActionId action = 0;
            int16_t value = 0;
            if (hitTest(x, y, action, value) && action == kActionNav && (value == 0 || value == 1) && value != page) {
                page = value;
                return PortalAction::Redraw;
            }
            return PortalAction::Stay;
        }
        default:
            return PortalAction::Stay;
    }
}
