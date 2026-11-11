#include "keyboard_ui.h"
#include "platform/hal/display.h"
#include "platform/hal/buttons.h"
#include "services/power_service.h"
#include "platform/hal/battery.h"
#include "ui/font.h"
#include "ui/i18n.h"
#include "ui/kit/ui_kit.h"
#include "actions.h"

// -----------------------------------------------------------------------------
// Keyboard layouts: 4 rows of 12 keys each, UTF-8 (a key is one character).
// The UI fonts carry ASCII, Latin-1 and Cyrillic: every key here is drawable.
// -----------------------------------------------------------------------------
struct Layout {
    const char* rows[4];
    const char* shifted[4];
    MsgId name;          // shown in the status line
    const char* label;   // on the layout key ("next layout")
};

static const Layout kLayouts[] = {
    {{"1234567890-=", "qwertyuiop[]", "asdfghjkl;'\\", "zxcvbnm,./_@"},
     {"!@#$%^&*()_+", "QWERTYUIOP{}", "ASDFGHJKL:\"|", "ZXCVBNM<>?~`"}, MsgId::KEYBOARD_LAYER_LOWER, "abc"},
    {{"1234567890-.", "йцукенгшщзхъ", "фывапролджэ,", "ячсмитьбюё_@"},
     {"!\"#;%:?*()_+", "ЙЦУКЕНГШЩЗХЪ", "ФЫВАПРОЛДЖЭ,", "ЯЧСМИТЬБЮЁ_@"}, MsgId::KEYBOARD_LAYER_CYR, "абв"},
    {{"1234567890¿¡", "áéíóúüñçàèìò", "ùâêîôûäëïöß'", "£«»¢§°\"-_.,@"},
     {"1234567890¿¡", "ÁÉÍÓÚÜÑÇÀÈÌÒ", "ÙÂÊÎÔÛÄËÏÖß'", "£«»¢§°\"-_.,@"}, MsgId::KEYBOARD_LAYER_ACC, "áéñ"},
    {{"1234567890!?", "@#$%^&*()-_+", "=[]{}\\|;:'\"~", "<>,./`:;!?-_"},
     {"1234567890!?", "@#$%^&*()-_+", "=[]{}\\|;:'\"~", "<>,./`:;!?-_"}, MsgId::KEYBOARD_LAYER_SYM, "#+="},
};
static constexpr int kLayoutCount = sizeof(kLayouts) / sizeof(kLayouts[0]);
static constexpr int kSymbolLayout = 3;

// The 12 keys of a row as separate UTF-8 characters.
static void splitRow(const char* row, char out[12][5]) {
    const char* p = row;
    for (int k = 0; k < 12; k++) {
        int n = 0;
        if (*p) {
            out[k][n++] = *p++;
            while (*p && ((uint8_t)*p & 0xC0) == 0x80 && n < 4) out[k][n++] = *p++;
        }
        out[k][n] = '\0';
    }
}

// Removes the last UTF-8 character.
static void eraseLast(String& s) {
    int at = (int)s.length() - 1;
    while (at > 0 && ((uint8_t)s[at] & 0xC0) == 0x80) at--;
    if (at >= 0) s.remove(at);
}

struct ActionBtnDef {
    int x;
    int w;
    const char* label;
    int fontScale;
};

bool KeyboardUI::show(const String& title, const String& subtitle, String& inOutText, KeyboardMode mode) {
    int selRow = 1;
    int selCol = 0;
    // Text in the interface's language starts in its alphabet; logins, passwords
    // and addresses in Latin letters.
    int layout = (mode == KB_MODE_TEXT && I18n::getLanguage() == AppLanguage::RU) ? 1 : 0;
    bool shift = false;
    String currentText = inOutText;
    char keyText[48][5];
    auto loadKeys = [&]() {
        const Layout& l = kLayouts[layout];
        for (int r = 0; r < 4; r++) splitRow(shift ? l.shifted[r] : l.rows[r], &keyText[r * 12]);
    };
    loadKeys();

    // Action Row Buttons Configuration (Row 4, 6 buttons aligned with 12 keys above)
    // Width: 64 + 2 + 64 + 2 + 108 + 2 + 56 + 2 + 82 + 2 + 80 = 464px. Margins: 8px left, 8px right.
    const ActionBtnDef actionBtns[6] = {
        { 8,   64,  "A/a",    1 },
        { 74,  64,  "abc",    1 },
        { 140, 108, tr(MsgId::KEYBOARD_BTN_SPACE), 1 },
        { 250, 56,  "< ⌫",   1 },
        { 308, 82,  tr(MsgId::KEYBOARD_BTN_CANCEL), 0 },
        { 392, 80,  tr(MsgId::KEYBOARD_BTN_OK),   1 }
    };

    auto renderKeyboard = [&]() {
        using namespace uikit;
        Pass pass;
        Frame& f = pass.frame();
        clear();
        Rect area = topBar(f, title.c_str());
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_ERASE), tr(MsgId::KEY_TYPE), "\xE2\x97\x80", "\xE2\x96\xB6"});
        area = insetXY(area, 8, 10);

        // 1. Context hint
        if (subtitle.length() > 0) {
            text(insetX(takeTop(area, (int16_t)(lineHeight(FontBody) + 6)), 8), subtitle.c_str(), FontBody);
        }
        takeTop(area, 6);

        // 2. Text input box with a trailing cursor; long text keeps its tail visible.
        Rect box = takeTop(area, (int16_t)(lineHeight(FontBody) + 24));
        frameRect(box, Color::Black, 2);
        const int16_t maxTextW = (int16_t)(box.width - 24);
        String visibleStr = currentText + "_";
        if (textWidth(FontBody, visibleStr.c_str()) > maxTextW) {
            for (int cut = 1; cut < (int)currentText.length(); cut++) {
                if (((uint8_t)currentText[cut] & 0xC0) == 0x80) continue;   // inside a character
                String testStr = "..." + currentText.substring(cut) + "_";
                if (textWidth(FontBody, testStr.c_str()) <= maxTextW) {
                    visibleStr = testStr;
                    break;
                }
            }
        }
        text(insetX(box, 12), visibleStr.c_str(), FontBody);

        // 3. Length and layer status
        Rect info = insetX(takeTop(area, (int16_t)(lineHeight(FontSmall) + 8)), 4);
        char lenBuf[64];
        int chars = 0;   // characters, not bytes
        for (size_t i = 0; i < currentText.length(); i++) chars += ((uint8_t)currentText[i] & 0xC0) != 0x80;
        snprintf(lenBuf, sizeof(lenBuf), tr(MsgId::KEYBOARD_LEN_FMT), chars);
        const char* layerDesc = (shift && layout == 0) ? tr(MsgId::KEYBOARD_LAYER_UPPER) : tr(kLayouts[layout].name);
        text(info, lenBuf, FontSmall);
        text(info, layerDesc, FontSmall, Color::Black, TextAlign::Right);
        takeTop(area, 12);

        // 4. Character grid (12 x 4)
        fi::KeyGridKey keys[48];
        for (int i = 0; i < 48; i++) {
            keys[i].label = keyText[i];
            keys[i].value = (int16_t)i;
        }
        const int16_t keyH = 54, gapY = 8;
        fi::KeyGridProps kp;
        kp.keys = keys;
        kp.rows = 4;
        kp.cols = 12;
        kp.selectedIndex = (selRow < 4) ? (int16_t)(selRow * 12 + selCol) : (int16_t)-1;
        kp.action = 1;
        kp.labelText = style(FontBody, Color::Black, TextAlign::Center);
        kp.keyStyles = fi::defaultKeyStyles();
        kp.gap = 3;
        kp.radius = 3;
        fi::keyGrid(f, takeTop(area, (int16_t)(4 * keyH + 3 * gapY)), kp);
        takeTop(area, gapY);

        // 5. Action row: widths keep the proportions of the original layout.
        Rect row = takeTop(area, keyH);
        const int weights[6] = { 64, 64, 108, 56, 82, 80 };
        const int totalW = 454;  // sum of weights
        int16_t x = row.x;
        for (int b = 0; b < 6; b++) {
            const int16_t w = (b == 5) ? (int16_t)(row.right() - x)
                                       : (int16_t)((row.width - 5 * 2) * weights[b] / totalW);
            Rect btn{x, row.y, w, row.height};
            x = (int16_t)(x + w + 2);
            const char* label = actionBtns[b].label;
            if (b == 0) label = shift ? "a/A" : "A/a";
            else if (b == 1) label = kLayouts[(layout + 1) % kLayoutCount].label;   // the one it switches to
            const bool sel = (selRow == 4 && selCol == b);
            const fi::FontId font = actionBtns[b].fontScale == 0 ? FontSmall : FontBody;
            if (sel) fill(btn, Color::Black, 3);
            else frameRect(btn, Color::Black, 1, 3);
            text(btn, label, font, sel ? Color::White : Color::Black, TextAlign::Center);
            f.hit(btn, 2, (int16_t)b);
        }

        present(false);
    };

    renderKeyboard();
    Buttons.clearEvents();

    while (true) {
        ButtonEvent event;

        if (PowerService::pollModalEvent(event)) {
            // Touch: ◀ in the title or a swipe to the right leaves without saving
            // (Picco has no BACK key).
            if (event.action == ACTION_CLICK &&
                ((event.id == BTN_TAP && uikit::backTapped(event.x, event.y)) || event.id == BTN_SWIPE_RIGHT)) {
                return false;
            }
            if (event.action == ACTION_CLICK && event.id == BTN_TAP) {
                // Tap on a key: select it and type it (CONFIRM below).
                freeink::ui::ActionId action = 0;
                int16_t value = 0;
                if (uikit::hitTest(event.x, event.y, action, value)) {
                    if (action == 1 && value >= 0 && value < 48) { selRow = value / 12; selCol = value % 12; event.id = BTN_CONFIRM; }
                    else if (action == 2 && value >= 0 && value < 6) { selRow = 4; selCol = value; event.id = BTN_CONFIRM; }
                }
            }
            if (event.action == ACTION_CLICK) {
                const Action key = Actions::of(event);
                if (key == Action::Up) {
                    if (selRow == 0) {
                        selRow = 4;
                        selCol = selCol / 2;
                    } else if (selRow == 4) {
                        selRow = 3;
                        selCol = selCol * 2;
                    } else {
                        selRow--;
                    }
                    renderKeyboard();
                } else if (key == Action::Down) {
                    if (selRow == 4) {
                        selRow = 0;
                        selCol = selCol * 2;
                    } else if (selRow == 3) {
                        selRow = 4;
                        selCol = selCol / 2;
                    } else {
                        selRow++;
                    }
                    renderKeyboard();
                } else if (key == Action::Prev) {
                    int maxCols = (selRow == 4) ? 6 : 12;
                    selCol = (selCol - 1 + maxCols) % maxCols;
                    renderKeyboard();
                } else if (key == Action::Next) {
                    int maxCols = (selRow == 4) ? 6 : 12;
                    selCol = (selCol + 1) % maxCols;
                    renderKeyboard();
                } else if (key == Action::Select) {
                    if (selRow < 4) {
                        const char* ch = keyText[selRow * 12 + selCol];
                        if (*ch) {
                            currentText += ch;
                            renderKeyboard();
                        }
                    } else {
                        // Action row buttons (0..5)
                        if (selCol == 0) { // A/a Shift toggle
                            shift = !shift && layout != kSymbolLayout;
                            loadKeys();
                            renderKeyboard();
                        } else if (selCol == 1) { // the next layout: Latin, Cyrillic, accents, symbols
                            layout = (layout + 1) % kLayoutCount;
                            if (layout == kSymbolLayout) shift = false;
                            loadKeys();
                            renderKeyboard();
                        } else if (selCol == 2) { // Space
                            currentText += ' ';
                            renderKeyboard();
                        } else if (selCol == 3) { // Backspace
                            if (currentText.length() > 0) {
                                eraseLast(currentText);
                                renderKeyboard();
                            }
                        } else if (selCol == 4) { // Cancel
                            return false;
                        } else if (selCol == 5) { // OK / Done
                            inOutText = currentText;
                            return true;
                        }
                    }
                } else if (key == Action::Back) {
                    if (currentText.length() > 0) {
                        eraseLast(currentText);
                        renderKeyboard();
                    } else {
                        // Empty string + back = cancel
                        return false;
                    }
                }
            }
        }
    }
}
