#include "ui_kit.h"
#include "../i18n.h"

#include <BoardConfig.h>  // FREEINK_CAP_TOUCH

#include <new>
#include <stdio.h>
#include <string.h>

#include "../../hal/battery.h"
#include "../../hal/display.h"
#include "ui_fonts.h"

namespace uikit {

namespace {

// DisplayTarget lives in static storage: it can only be constructed once the
// framebuffer exists (after Display.begin()).
alignas(fi::DisplayTarget) unsigned char g_targetStorage[sizeof(fi::DisplayTarget)];
fi::DisplayTarget* g_target = nullptr;
fi::DeviceContext g_device{};
fi::InteractionBuffer<kMaxHits> g_hits;
Rect g_backBand{};  // title bar of the last topBar() screen

// Serves the symbol glyphs (arrows, check mark, dashes...) that the Carlito
// tables lack, at the pixel size of the font that asked (its yAdvance).
class SymbolSource final : public fi::RuntimeGlyphSource {
public:
    bool glyph(uint32_t cp, uint16_t pixelSize, Glyph* out) override {
        const fonts::SymbolSet* set = nullptr;
        const fonts::SymbolGlyph* g = find(cp, pixelSize, set);
        if (!g) return false;
        const fi::FontGlyph& m = g->glyph;
        const int n = m.width * m.height;
        if (n > (int)sizeof(cov_)) return false;
        // 1bpp MSB-first -> 8-bit coverage (0 or 255).
        for (int i = 0; i < n; ++i) {
            const uint8_t byte = set->bitmap[m.bitmapOffset + (i >> 3)];
            cov_[i] = (byte & (0x80 >> (i & 7))) ? 255 : 0;
        }
        out->coverage = cov_;
        out->width = m.width;
        out->height = m.height;
        out->xoff = m.xOffset;
        out->yoff = m.yOffset;
        out->advance = m.xAdvance;
        return true;
    }

    int16_t advance(uint32_t cp, uint16_t pixelSize) override {
        const fonts::SymbolSet* set = nullptr;
        const fonts::SymbolGlyph* g = find(cp, pixelSize, set);
        return g ? g->glyph.xAdvance : 0;
    }

private:
    static const fonts::SymbolGlyph* find(uint32_t cp, uint16_t pixelSize, const fonts::SymbolSet*& setOut) {
        for (const auto& set : fonts::kSymbolSets) {
            if (set.pixelSize != pixelSize) continue;
            for (uint8_t i = 0; i < set.count; ++i) {
                if (set.glyphs[i].codepoint == cp) {
                    setOut = &set;
                    return &set.glyphs[i];
                }
            }
        }
        return nullptr;
    }

    uint8_t cov_[48 * 48];
};

SymbolSource g_symbols;

fi::Paint solid(Color c) { return fi::Paint::solid(c); }

Color inverse(Color c) { return c == Color::Black ? Color::White : Color::Black; }

}  // namespace

// ---------------------------------------------------------------------------
void begin() {
    if (g_target) return;
    uint8_t* fb = Display.getBuffer();
    const int16_t pw = (int16_t)Display.rawWidth();
    const int16_t ph = (int16_t)Display.rawHeight();
    g_target = new (g_targetStorage) fi::DisplayTarget(fb, pw, ph, (int16_t)(pw / 8), fi::Orientation::Portrait);
    g_target->setFont(FontSmall, fonts::kUiSmallFont);
    g_target->setFont(FontBody, fonts::kUiBodyFont);
    g_target->setFont(FontTitle, fonts::kUiTitleFont);
    g_target->setFont(FontLarge, fonts::kUiLargeFont);
    g_target->setGlyphFallback(&g_symbols);
    g_device = g_target->deviceContext();
    g_device.hasTouch = false;
    g_device.hasButtons = true;
    Serial.printf("[UI] FreeInkUI target %dx%d (panel %dx%d)\n", g_device.width, g_device.height, pw, ph);
}

bool ready() { return g_target != nullptr; }

fi::DisplayTarget& target() {
    if (!g_target) begin();
    return *g_target;
}

const fi::DeviceContext& device() {
    if (!g_target) begin();
    return g_device;
}

int16_t width() { return device().width; }
int16_t height() { return device().height; }
Rect screen() { return device().screen(); }

void clear(Color color) { target().fill(screen(), solid(color)); }

void present(bool fullRefresh) { Display.update(fullRefresh ? REFRESH_FULL : REFRESH_PARTIAL); }

bool periodicFullRefresh() {
    static uint8_t n = 0;
    return (++n % 8) == 0;
}

Pass::Pass() : frame_(target(), device(), input_, g_hits) { g_backBand = Rect{}; }

bool backTapped(int16_t x, int16_t y) { return g_backBand.contains(x, y); }

bool touchOnly() {
#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
    return true;
#else
    return false;
#endif
}

bool touchUi() {
#if defined(FREEINK_CAP_TOUCH) && FREEINK_CAP_TOUCH || defined(UI_FAKE_FRONTLIGHT)
    return true;
#else
    return false;
#endif
}

bool hitTest(int16_t x, int16_t y, fi::ActionId& action, int16_t& value) {
    const fi::Interaction* items = g_hits.data();
    for (size_t i = g_hits.count(); i-- > 0;) {  // last registered = drawn on top
        if (items[i].rect.contains(x, y)) {
            action = items[i].action;
            value = items[i].value;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
fi::TextStyle style(fi::FontId font, Color color, TextAlign align, uint8_t maxLines) {
    fi::TextStyle s;
    s.font = font;
    s.color = color;
    s.align = align;
    s.maxLines = maxLines;
    return s;
}

int16_t lineHeight(fi::FontId font) { return target().lineHeight(font); }

int16_t textWidth(fi::FontId font, const char* str) {
    if (!str || !*str) return 0;
    return target().measureText(font, str, style(font)).width;
}

void text(Rect rect, const char* str, fi::FontId font, Color color, TextAlign align, uint8_t maxLines) {
    if (!str || !*str || rect.empty()) return;
    target().text(rect, str, style(font, color, align, maxLines));
}

int16_t wrappedHeight(const char* str, fi::FontId font, int16_t w, uint8_t maxLines) {
    if (!str || !*str) return 0;
    return fi::measureWrappedText(target(), str, style(font, Color::Black, TextAlign::Left, maxLines), w).height;
}

// ---------------------------------------------------------------------------
Rect takeTop(Rect& area, int16_t h) {
    if (h > area.height) h = area.height;
    Rect r{area.x, area.y, area.width, h};
    area.y = (int16_t)(area.y + h);
    area.height = (int16_t)(area.height - h);
    return r;
}

Rect takeBottom(Rect& area, int16_t h) {
    if (h > area.height) h = area.height;
    area.height = (int16_t)(area.height - h);
    return Rect{area.x, (int16_t)(area.y + area.height), area.width, h};
}

Rect insetX(Rect r, int16_t dx) { return r.inset(fi::Insets{0, dx, 0, dx}); }
Rect insetXY(Rect r, int16_t dx, int16_t dy) { return r.inset(fi::Insets{dy, dx, dy, dx}); }

void fill(Rect r, Color color, uint8_t radius) { target().fill(r, solid(color), radius); }

void frameRect(Rect r, Color color, uint8_t w, uint8_t radius) { target().stroke(r, solid(color), w, radius); }

void hline(int16_t x0, int16_t x1, int16_t y, Color color, uint8_t w) {
    target().fill(Rect{x0, y, (int16_t)(x1 - x0), (int16_t)w}, solid(color));
}

void vline(int16_t x, int16_t y0, int16_t y1, Color color, uint8_t w) {
    target().fill(Rect{x, y0, (int16_t)w, (int16_t)(y1 - y0)}, solid(color));
}

// ---------------------------------------------------------------------------
void battery(Frame& f, Rect r, Color color) {
    char label[12];
    snprintf(label, sizeof(label), "%d%%%s", Battery.getPercentage(), Battery.isCharging() ? "+" : "");
    fi::BatteryIndicatorProps b;
    b.percent = (uint8_t)(Battery.getPercentage() < 0 ? 0 : (Battery.getPercentage() > 100 ? 100 : Battery.getPercentage()));
    b.charging = Battery.isCharging();
    b.color = color;
    b.glyphWidth = 24;
    b.glyphHeight = 12;
    const int16_t glyphW = 24 + 2;
    Rect glyph{(int16_t)(r.right() - glyphW), r.y, glyphW, r.height};
    fi::batteryIndicator(f, glyph, b);
    Rect lbl{r.x, r.y, (int16_t)(r.width - glyphW - 6), r.height};
    text(lbl, label, FontSmall, color, TextAlign::Right);
}

Rect topBar(Frame& f, const char* title) {
    Rect area = screen();
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 18);
    Rect bar = takeTop(area, h);
    fill(bar, Color::Black);
    Rect inner = insetX(bar, kMargin);
    const int16_t batW = 92;
    battery(f, Rect{(int16_t)(inner.right() - batW), inner.y, batW, inner.height}, Color::White);
    Rect titleR{inner.x, inner.y, (int16_t)(inner.width - batW - kGap), inner.height};
#if defined(FREEINK_CAP_TOUCH) && FREEINK_CAP_TOUCH || defined(UI_FAKE_FRONTLIGHT)
    // Touch boards: the bar is the BACK control, so show it.
    text(Rect{titleR.x, titleR.y, 22, titleR.height}, "\xE2\x97\x80", FontBody, Color::White);  // U+25C0
    titleR.x = (int16_t)(titleR.x + 26);
    titleR.width = (int16_t)(titleR.width - 26);
#endif
    text(titleR, title, FontTitle, Color::White);
    g_backBand = bar;
    return area;
}

Rect appHeader(Frame& f) {
    Rect area = screen();
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 16);
    Rect bar = takeTop(area, h);
    Rect inner = insetX(bar, kMargin + 4);
    icon::logo(inner.x, (int16_t)(inner.y + (inner.height - 20) / 2), Color::Black);
    const int16_t batW = 92;
    text(Rect{(int16_t)(inner.x + 28), inner.y, (int16_t)(inner.width - 28 - batW), inner.height}, "book orbit",
         FontTitle);
    battery(f, Rect{(int16_t)(inner.right() - batW), inner.y, batW, inner.height}, Color::Black);
    hline(0, width(), (int16_t)(bar.bottom() - 1));
    return area;
}

Rect footerHints(Frame&, Rect area, const char* line1, const char* line2) {
    if (touchOnly() && ((line1 && *line1) || (line2 && *line2))) {
        // The screens' hints name LEFT / RIGHT / OK / BACK, which this board lacks.
        line1 = tr(MsgId::TOUCH_HINT_L1);
        line2 = tr(MsgId::TOUCH_HINT_L2);
    }
    const int16_t innerW = (int16_t)(width() - 2 * (kMargin + 4));
    const int16_t h1 = (line1 && *line1) ? (int16_t)(wrappedHeight(line1, FontSmall, innerW, 2) + 4) : 0;
    const int16_t h2 = (line2 && *line2) ? (int16_t)(wrappedHeight(line2, FontSmall, innerW, 2) + 4) : 0;
    if (h1 + h2 == 0) return area;
    Rect foot = takeBottom(area, (int16_t)(h1 + h2 + 14));
    hline(0, width(), foot.y);
    Rect inner = insetXY(foot, kMargin + 4, 8);
    if (h1) text(takeTop(inner, h1), line1, FontSmall, Color::Black, TextAlign::Left, 2);
    if (h2) text(takeTop(inner, h2), line2, FontSmall, Color::Black, TextAlign::Left, 2);
    return area;
}

int16_t pillWidth(const char* label) { return (int16_t)(textWidth(FontSmall, label) + 20); }

void pill(Rect r, const char* label, bool dark, bool onInverted) {
    // On a black row the roles swap: a "dark" pill becomes white.
    const Color ink = onInverted ? Color::White : Color::Black;
    const Color paper = inverse(ink);
    if (dark) {
        fill(r, ink, 4);
        text(r, label, FontSmall, paper, TextAlign::Center);
    } else {
        frameRect(r, ink, 1, 4);
        text(r, label, FontSmall, ink, TextAlign::Center);
    }
}

void card(Frame& f, Rect r, const char* title, const char* subtitle, const char* tag, bool selected,
          fi::ActionId action, int16_t value) {
    const Color ink = selected ? Color::White : Color::Black;
    if (selected) {
        fill(r, Color::Black);
    } else {
        fill(r, Color::White);
        frameRect(r, Color::Black, 1);
    }
    Rect inner = insetXY(r, 12, 8);
    int16_t tagW = 0;
    if (tag && *tag) {
        tagW = pillWidth(tag);
        const int16_t ph = (int16_t)(lineHeight(FontSmall) + 6);
        pill(Rect{(int16_t)(inner.right() - tagW), inner.y, tagW, ph}, tag, true, selected);
    }
    const int16_t th = lineHeight(FontTitle);
    Rect titleR{inner.x, inner.y, (int16_t)(inner.width - tagW - (tagW ? kGap : 0)), th};
    text(titleR, title, FontTitle, ink);
    if (subtitle && *subtitle) {
        Rect sub{inner.x, (int16_t)(inner.y + th + 4), inner.width, (int16_t)(inner.bottom() - inner.y - th - 4)};
        text(sub, subtitle, FontSmall, ink, TextAlign::Left, 1);
    }
    if (action != fi::NO_ACTION) f.hit(r, action, value);
}

void progress(Frame& f, Rect r, int32_t permille) {
    fi::ProgressBarProps p;
    p.value = permille < 0 ? 0 : (permille > 1000 ? 1000 : permille);
    p.max = 1000;
    p.border = solid(Color::Black);
    p.borderWidth = 1;
    fi::progressBar(f, r, p);
    // Inner fill inset by 2px so the outline stays visible.
    if (p.value > 0 && r.width > 4 && r.height > 4) {
        Rect in = insetXY(r, 2, 2);
        fill(Rect{in.x, in.y, (int16_t)((int32_t)in.width * p.value / 1000), in.height}, Color::Black);
    }
}

Rect modalPanel(const char* title, int16_t w, int16_t h) {
    if (w > width() - 2 * kMargin) w = (int16_t)(width() - 2 * kMargin);
    if (h > height() - 2 * kMargin) h = (int16_t)(height() - 2 * kMargin);
    Rect box{(int16_t)((width() - w) / 2), (int16_t)((height() - h) / 2), w, h};
    fill(Rect{(int16_t)(box.x + 6), (int16_t)(box.y + 6), w, h}, Color::Black);  // drop shadow
    fill(box, Color::White);
    frameRect(box, Color::Black, 2);
    Rect area = box.inset(fi::Insets{2, 2, 2, 2});
    const int16_t bandH = (int16_t)(lineHeight(FontTitle) + 14);
    Rect band = takeTop(area, bandH);
    fill(band, Color::Black);
    text(insetX(band, 14), title, FontTitle, Color::White);
    return insetXY(area, 18, 10);
}

const char* deviceDisplayName() {
#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
    return "BOOX PICCO";
#elif defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
    return "XTEINK X4 PRO";
#elif defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
    return "XTEINK X4 CLASSIC";
#elif defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
    return "XTEINK X3";
#else
    return "XTEINK X4";
#endif
}

// ---------------------------------------------------------------------------
namespace icon {

void logo(int16_t x, int16_t y, Color c) {
    // Twin bookmark ribbons.
    frameRect(Rect{x, y, 8, 20}, c, 1, 3);
    fill(Rect{(int16_t)(x + 2), (int16_t)(y + 2), 4, 16}, c, 2);
    frameRect(Rect{(int16_t)(x + 10), (int16_t)(y + 2), 8, 18}, c, 1, 3);
    fill(Rect{(int16_t)(x + 12), (int16_t)(y + 4), 4, 14}, c, 2);
}

void folder(int16_t x, int16_t y, Color c) {
    fill(Rect{x, y, 6, 2}, c);
    frameRect(Rect{x, (int16_t)(y + 2), 14, 9}, c);
    fill(Rect{(int16_t)(x + 1), (int16_t)(y + 3), 12, 2}, c);
}

void check(int16_t x, int16_t y, Color c) {
    target().line(fi::Point{x, (int16_t)(y + 5)}, fi::Point{(int16_t)(x + 3), (int16_t)(y + 9)}, 2, solid(c));
    target().line(fi::Point{(int16_t)(x + 3), (int16_t)(y + 9)}, fi::Point{(int16_t)(x + 10), y}, 2, solid(c));
}

void cloud(int16_t x, int16_t y, Color c) {
    // A solid cloud: two bumps over a rounded base (the old outline version
    // came out as a few dots at this size).
    fill(Rect{(int16_t)(x + 3), (int16_t)(y - 1), 8, 8}, c, 4);
    fill(Rect{(int16_t)(x + 8), (int16_t)(y + 1), 7, 7}, c, 3);
    fill(Rect{x, (int16_t)(y + 4), 16, 7}, c, 3);
}

void sync(int16_t x, int16_t y, Color c) {
    // Two half rings with arrow heads.
    frameRect(Rect{(int16_t)(x + 1), (int16_t)(y + 1), 18, 18}, c, 2, 9);
    fill(Rect{(int16_t)(x + 13), (int16_t)(y + 3), 7, 6}, inverse(c));
    fill(Rect{x, (int16_t)(y + 11), 7, 6}, inverse(c));
    target().triangle(fi::Point{(int16_t)(x + 12), (int16_t)(y + 1)}, fi::Point{(int16_t)(x + 19), (int16_t)(y + 5)},
                      fi::Point{(int16_t)(x + 12), (int16_t)(y + 9)}, solid(c));
    target().triangle(fi::Point{(int16_t)(x + 8), (int16_t)(y + 11)}, fi::Point{(int16_t)(x + 1), (int16_t)(y + 15)},
                      fi::Point{(int16_t)(x + 8), (int16_t)(y + 19)}, solid(c));
}

void signal(int16_t x, int16_t y, int bars, Color c) {
    for (int b = 0; b < 4; ++b) {
        const int16_t bh = (int16_t)((b + 1) * 5);
        Rect r{(int16_t)(x + b * 6), (int16_t)(y + 20 - bh), 4, bh};
        if (b < bars) fill(r, c);
        else frameRect(r, c);
    }
}

}  // namespace icon

}  // namespace uikit
