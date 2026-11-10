#include "appearance.h"

#include "../../reader/reader.h"
#include "../../ui/font.h"
#include "../../ui/i18n.h"

namespace Appearance {

namespace {

const int kLineSpacing[] = {80, 90, 100, 110, 120, 130, 140, 150, 170, 200};
const int kParagraphSpacing[] = {0, 25, 50, 75, 100, 150, 200};
const int kMargins[] = {6, 10, 14, 20, 26, 32, 40, 50};
const int kRefresh[] = {1, 2, 3, 5, 8, 10, 15, 20, 0};

// The next value in a list (the nearest one for a value not in it).
template <size_t N>
int stepIn(const int (&list)[N], int current, int dir) {
    size_t at = 0;
    int best = 1 << 30;
    for (size_t i = 0; i < N; i++) {
        const int d = abs(list[i] - current);
        if (d < best) best = d, at = i;
    }
    if (dir > 0) at = at + 1 < N ? at + 1 : 0;
    else at = at > 0 ? at - 1 : N - 1;
    return list[at];
}

const char* onOff(bool on) { return tr(on ? MsgId::APPEAR_ON : MsgId::APPEAR_OFF); }

}  // namespace

const char* label(int row) {
    switch (row) {
        case TYPEFACE: return tr(MsgId::APPEAR_TYPEFACE);
        case SIZE: return tr(MsgId::APPEAR_SIZE);
        case LINE_SPACING: return tr(MsgId::APPEAR_LINE_SPACING);
        case PARAGRAPH_SPACING: return tr(MsgId::APPEAR_PARAGRAPH_SPACING);
        case MARGINS: return tr(MsgId::APPEAR_MARGINS);
        case ALIGNMENT: return tr(MsgId::APPEAR_ALIGNMENT);
        case HYPHENATION: return tr(MsgId::APPEAR_HYPHENATION);
        case KEEP_LINES: return tr(MsgId::APPEAR_KEEP_LINES);
        case BOOK_STYLES: return tr(MsgId::APPEAR_BOOK_STYLES);
        case FOCUS_READING: return tr(MsgId::APPEAR_FOCUS_READING);
        case IMAGES: return tr(MsgId::APPEAR_IMAGES);
        case FULL_REFRESH: return tr(MsgId::APPEAR_FULL_REFRESH);
    }
    return "";
}

const char* value(const AppConfig& cfg, int row, char* buf, size_t len) {
    switch (row) {
        case TYPEFACE: snprintf(buf, len, "%s", Reader.fontFamilyName().c_str()); break;
        case SIZE: snprintf(buf, len, "%s", BookReader::getFontSizeName(cfg.readerFontSize)); break;
        case LINE_SPACING: snprintf(buf, len, "%d%%", cfg.lineSpacing); break;
        case PARAGRAPH_SPACING: snprintf(buf, len, "%d%%", cfg.paragraphSpacing); break;
        case MARGINS: snprintf(buf, len, tr(MsgId::APPEAR_MARGINS_FMT), cfg.marginPx); break;
        case ALIGNMENT:
            snprintf(buf, len, "%s", tr(cfg.justify ? MsgId::APPEAR_ALIGN_JUSTIFY : MsgId::APPEAR_ALIGN_LEFT));
            break;
        case HYPHENATION: snprintf(buf, len, "%s", onOff(cfg.hyphenation)); break;
        case KEEP_LINES: snprintf(buf, len, "%s", onOff(cfg.keepLines)); break;
        case BOOK_STYLES: snprintf(buf, len, "%s", onOff(cfg.bookStyles)); break;
        case FOCUS_READING: snprintf(buf, len, "%s", onOff(cfg.focusReading)); break;
        case IMAGES:
            snprintf(buf, len, "%s", tr(cfg.sharpImages ? MsgId::APPEAR_IMAGES_SHARP : MsgId::APPEAR_IMAGES_DITHERED));
            break;
        case FULL_REFRESH:
            if (cfg.fullRefreshEvery <= 0) snprintf(buf, len, "%s", tr(MsgId::APPEAR_REFRESH_NEVER));
            else snprintf(buf, len, tr(MsgId::APPEAR_REFRESH_FMT), cfg.fullRefreshEvery);
            break;
        default: buf[0] = 0;
    }
    return buf;
}

bool step(AppConfig& cfg, int row, int dir) {
    switch (row) {
        case SIZE:
            cfg.readerFontSize = (cfg.readerFontSize + (dir > 0 ? 1 : FontEngine::kSizeCount - 1)) % FontEngine::kSizeCount;
            return true;
        case LINE_SPACING: cfg.lineSpacing = stepIn(kLineSpacing, cfg.lineSpacing, dir); return true;
        case PARAGRAPH_SPACING: cfg.paragraphSpacing = stepIn(kParagraphSpacing, cfg.paragraphSpacing, dir); return true;
        case MARGINS: cfg.marginPx = stepIn(kMargins, cfg.marginPx, dir); return true;
        case ALIGNMENT: cfg.justify = !cfg.justify; return true;
        case HYPHENATION: cfg.hyphenation = !cfg.hyphenation; return true;
        case KEEP_LINES: cfg.keepLines = !cfg.keepLines; return true;
        case BOOK_STYLES: cfg.bookStyles = !cfg.bookStyles; return true;
        case FOCUS_READING: cfg.focusReading = !cfg.focusReading; return true;
        case IMAGES: cfg.sharpImages = !cfg.sharpImages; return true;
        case FULL_REFRESH: {
            // "never" sits after the largest
            const int cur = cfg.fullRefreshEvery <= 0 ? 0 : cfg.fullRefreshEvery;
            size_t at = 0;
            for (size_t i = 0; i < sizeof(kRefresh) / sizeof(kRefresh[0]); i++)
                if (kRefresh[i] == cur) at = i;
            const size_t n = sizeof(kRefresh) / sizeof(kRefresh[0]);
            at = dir > 0 ? (at + 1) % n : (at + n - 1) % n;
            cfg.fullRefreshEvery = kRefresh[at];
            return true;
        }
        default: return false;
    }
}

BookReader::LayoutOptions layoutOptions(const AppConfig& cfg) {
    BookReader::LayoutOptions o;
    o.lineSpacingPct = (uint16_t)cfg.lineSpacing;
    o.paragraphSpacingPct = (uint16_t)cfg.paragraphSpacing;
    o.marginPx = (uint8_t)cfg.marginPx;
    o.justify = cfg.justify;
    o.hyphenate = cfg.hyphenation;
    o.keepLines = cfg.keepLines;
    o.bookStyles = cfg.bookStyles;
    o.focusReading = cfg.focusReading;
    return o;
}

}  // namespace Appearance
