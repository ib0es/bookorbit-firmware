#include "portal_view.h"

#include "../kit/ui_kit.h"
#include "../i18n.h"

// The web portal's screen, moved from WebConfigManager (A5).
void drawPortalScreen(const char* apPass) {
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
