#include "status_modal.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"

using namespace uikit;

// Centered dialog: black title band, a main line (body font, up to 2 lines),
// up to three secondary lines (small font) and an optional footer hint.
static void drawStatusModalImpl(const char* title, const char* line1, const char* line2, const char* line3,
                                const char* line4, const char* footerHint, bool fullRefresh, const KeyLabels* keys) {
    Pass pass;
    // Key hints ("[BACK] - Cancel", "[OK] Close") mean nothing on a touch-only
    // board; there any tap closes a message or cancels a network step.
    if (touchOnly() && footerHint && *footerHint) footerHint = tr(MsgId::TOUCH_MODAL_HINT);
    const int16_t w = (int16_t)(width() - 2 * (kMargin + 14));
    const int16_t innerW = (int16_t)(w - 2 * 20);

    const char* small[3] = {line2, line3, line4};
    int16_t bodyH = 0;
    if (line1 && *line1) bodyH += wrappedHeight(line1, FontBody, innerW, 2) + 12;
    for (const char* s : small) {
        if (s && *s) bodyH += wrappedHeight(s, FontSmall, innerW, 2) + 8;
    }
    const int16_t footH = (footerHint && *footerHint) ? (int16_t)(lineHeight(FontSmall) + 22) : 0;
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 14 + 4 + 20 + bodyH + footH + 16);

    Rect body = modalPanel(title, w, h);
    if (footH) {
        Rect foot = takeBottom(body, footH);
        hline(foot.x, foot.right(), (int16_t)(foot.y + 6));
        text(Rect{foot.x, (int16_t)(foot.y + 10), foot.width, (int16_t)(foot.height - 10)}, footerHint, FontSmall,
             Color::Black, TextAlign::Center);
    }
    takeTop(body, 6);
    if (line1 && *line1) {
        const int16_t lh = wrappedHeight(line1, FontBody, body.width, 2);
        text(takeTop(body, lh), line1, FontBody, Color::Black, TextAlign::Left, 2);
        takeTop(body, 12);
    }
    for (const char* s : small) {
        if (!s || !*s) continue;
        const int16_t lh = wrappedHeight(s, FontSmall, body.width, 2);
        text(takeTop(body, lh), s, FontSmall, Color::Black, TextAlign::Left, 2);
        takeTop(body, 8);
    }
    if (keys) keyHints(pass.frame(), Rect{0, 0, width(), height()}, *keys);
    present(fullRefresh);
}

void drawStatusModal(const char* title, const char* line1, const char* line2, const char* line3, const char* line4,
                     const char* footerHint, bool fullRefresh) {
    drawStatusModalImpl(title, line1, line2, line3, line4, footerHint, fullRefresh, nullptr);
}

void drawConfirmModal(const char* title, const char* line1, const char* line2, const char* line3,
                      const char* footerHint, const char* okLabel, const char* cancelLabel) {
    if (!touchUi()) {
        const KeyLabels keys{cancelLabel, okLabel, "", ""};
        drawStatusModalImpl(title, line1, line2, line3, nullptr, footerHint, false, &keys);
        return;
    }
    Pass pass;
    Frame& f = pass.frame();
    const int16_t w = (int16_t)(width() - 2 * (kMargin + 14));
    const int16_t innerW = (int16_t)(w - 2 * 20);

    const char* small[2] = {line2, line3};
    int16_t bodyH = 0;
    if (line1 && *line1) bodyH += wrappedHeight(line1, FontBody, innerW, 2) + 12;
    for (const char* s : small) {
        if (s && *s) bodyH += wrappedHeight(s, FontSmall, innerW, 2) + 8;
    }
    const int16_t btnH = (int16_t)(lineHeight(FontBody) + 24);  // finger-sized
    const int16_t footH = (int16_t)(btnH + 16);
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 14 + 4 + 20 + bodyH + footH + 16);

    Rect body = modalPanel(title, w, h);
    Rect foot = takeBottom(body, footH);
    Rect row{foot.x, (int16_t)(foot.y + 12), foot.width, btnH};
    const int16_t bw = (int16_t)((row.width - kGap * 2) / 2);
    Rect cancel{row.x, row.y, bw, btnH};
    Rect ok{(int16_t)(row.right() - bw), row.y, bw, btnH};
    frameRect(cancel, Color::Black, 2);
    text(cancel, cancelLabel, FontBody, Color::Black, TextAlign::Center);
    fill(ok, Color::Black, 4);
    text(ok, okLabel, FontBody, Color::White, TextAlign::Center);
    // Generous hit areas: the whole half of the footer row each.
    f.hit(Rect{foot.x, foot.y, (int16_t)(foot.width / 2), foot.height}, MODAL_HIT_CANCEL);
    f.hit(Rect{(int16_t)(foot.x + foot.width / 2), foot.y, (int16_t)(foot.width - foot.width / 2), foot.height},
          MODAL_HIT_OK);

    takeTop(body, 6);
    if (line1 && *line1) {
        const int16_t lh = wrappedHeight(line1, FontBody, body.width, 2);
        text(takeTop(body, lh), line1, FontBody, Color::Black, TextAlign::Left, 2);
        takeTop(body, 12);
    }
    for (const char* s : small) {
        if (!s || !*s) continue;
        const int16_t lh = wrappedHeight(s, FontSmall, body.width, 2);
        text(takeTop(body, lh), s, FontSmall, Color::Black, TextAlign::Left, 2);
        takeTop(body, 8);
    }
    present(false);
}
