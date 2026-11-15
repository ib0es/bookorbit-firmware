#include "sleep_view.h"

#include <math.h>
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../jpeg_viewer.h"

namespace {
void drawSleepStatusBar(uikit::Frame& f, const SleepScreen& s, const char* banner, int16_t W, int16_t H) {
    using namespace uikit;
    const int16_t statusBarH = (int16_t)(lineHeight(FontBody) + 18);
    const int16_t statusBarY = (int16_t)(H - statusBarH);

    Rect bar{0, statusBarY, W, statusBarH};
    fill(bar, Color::White);
    hline(0, W, statusBarY);
    Rect in = insetX(bar, kMargin);
    battery(f, Rect{(int16_t)(in.right() - 92), in.y, 92, in.height}, Color::Black);
    const char* textStr = (banner && *banner) ? banner : tr(MsgId::POWER_SLEEP_BANNER);
    const int16_t bw = (int16_t)(textWidth(FontBody, textStr) + 16);
    text(Rect{in.x, in.y, bw, in.height}, textStr, FontBody);
    if (s.progress > 0.0f) {
        char progStr[32];
        snprintf(progStr, sizeof(progStr), tr(MsgId::POWER_SLEEP_READ_FMT), (int)roundf(s.progress * 100.0f));
        text(Rect{(int16_t)(in.x + bw), in.y, (int16_t)(in.width - bw - 100), in.height}, progStr, FontSmall);
    }
}
}  // namespace

// The sleep screen, moved from PowerService (A5): the book's cover with a
// status bar, or the device's name and how to wake it.
void drawSleepScreen(const SleepScreen& s) {
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    clear();
    const int16_t W = width(), H = height();

    bool drewCover = false;
    if (s.withCover) {
        const int16_t statusBarH = (int16_t)(lineHeight(FontBody) + 18);
        const int16_t statusBarY = (int16_t)(H - statusBarH);

        if (s.coverPath && *s.coverPath) {
            drewCover = JpegViewer::drawCover(s.coverPath, 0, 0, W, statusBarY,
                                              s.coverCrop ? COVER_FIT_CROP : COVER_FIT_SCALE);
        }
        if (!drewCover && s.title && *s.title) {
            Serial.println("[Power] Drawing sleep cover placeholder");
            JpegViewer::drawCoverPlaceholder(s.title, s.author, kMargin, kMargin, W - 2 * kMargin,
                                             statusBarY - 2 * kMargin);
            drewCover = true;
        }
        if (drewCover) {
            drawSleepStatusBar(f, s, s.banner, W, H);
        }
    }

    if (!drewCover) {
        Rect box{30, (int16_t)(H / 2 - 160), (int16_t)(W - 60), 320};
        frameRect(box, Color::Black, 2, kRadius);
        Rect in = insetXY(box, 20, 30);
        text(takeTop(in, (int16_t)(lineHeight(FontLarge) + 8)), deviceDisplayName(), FontLarge, Color::Black,
             TextAlign::Center);
        text(takeTop(in, (int16_t)(lineHeight(FontLarge) + 8)),
             (s.banner && *s.banner) ? s.banner : tr(MsgId::POWER_SLEEP_BANNER), FontLarge, Color::Black,
             TextAlign::Center);
        takeTop(in, 16);
        hline((int16_t)(in.x + 20), (int16_t)(in.right() - 20), in.y);
        takeTop(in, 24);
        text(takeTop(in, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::POWER_WAKE_HINT_L1), FontBody, Color::Black,
             TextAlign::Center);
        text(takeTop(in, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::POWER_WAKE_HINT_L2), FontBody, Color::Black,
             TextAlign::Center);
    }
}

void updateSleepBanner(const SleepScreen& s, const char* banner) {
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    const int16_t W = width(), H = height();

    const bool hasCover = s.withCover && ((s.coverPath && *s.coverPath) || (s.title && *s.title));
    if (hasCover) {
        drawSleepStatusBar(f, s, banner, W, H);
    } else {
        SleepScreen copy = s;
        copy.banner = banner;
        drawSleepScreen(copy);
    }
}
