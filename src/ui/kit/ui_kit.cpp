#include "ui_kit.h"
#include "../i18n.h"

#include "../../platform/device.h"

#include <algorithm>
#include <new>
#include <stdio.h>
#include <string.h>

#include "../../platform/hal/battery.h"
#include "../../platform/hal/display.h"
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
ListPager g_pager{};  // the paged list of the last drawn screen

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

// Bits x0..x1 (inclusive, MSB first) of a panel row set (white) or cleared (black).
inline void paintSpan(uint8_t* row, int x0, int x1, bool white) {
    const int b0 = x0 >> 3, b1 = x1 >> 3;
    const uint8_t m0 = (uint8_t)(0xFF >> (x0 & 7));
    const uint8_t m1 = (uint8_t)(0xFF << (7 - (x1 & 7)));
    if (b0 == b1) {
        const uint8_t m = m0 & m1;
        row[b0] = white ? (uint8_t)(row[b0] | m) : (uint8_t)(row[b0] & ~m);
        return;
    }
    row[b0] = white ? (uint8_t)(row[b0] | m0) : (uint8_t)(row[b0] & ~m0);
    if (b1 - b0 > 1) memset(row + b0 + 1, white ? 0xFF : 0x00, (size_t)(b1 - b0 - 1));
    row[b1] = white ? (uint8_t)(row[b1] | m1) : (uint8_t)(row[b1] & ~m1);
}

// A solid black or white rectangle straight into the framebuffer, a byte at a
// time along the panel's rows: DisplayTarget::fill plots every pixel with its
// rotation (384,000 calls for a cleared screen, ~0.2 s of every menu redraw on
// the X4). The same pixels as plot(): the rectangle is clipped to the logical
// screen and the target's clip, then turned into the panel's frame. False for
// other colours (dithered grays), which go the slow way.
bool fastFill(Rect r, Color c) {
    if (c != Color::Black && c != Color::White) return false;
    fi::DisplayTarget& t = target();
    uint8_t* fb = Display.getBuffer();
    if (!fb) return false;
    const Rect clip = t.clipRect();
    int32_t x0 = std::max<int32_t>({r.x, clip.x, 0});
    int32_t y0 = std::max<int32_t>({r.y, clip.y, 0});
    int32_t x1 = std::min<int32_t>({(int32_t)r.x + r.width, (int32_t)clip.x + clip.width, (int32_t)g_device.width}) - 1;
    int32_t y1 =
            std::min<int32_t>({(int32_t)r.y + r.height, (int32_t)clip.y + clip.height, (int32_t)g_device.height}) - 1;
    if (x1 < x0 || y1 < y0) return true;
    const int32_t pw = Display.rawWidth(), ph = Display.rawHeight(), pwb = pw / 8;
    // The two corners in the panel's frame (as DisplayTarget::plot turns them).
    auto toPanel = [&](int32_t x, int32_t y, int32_t& px, int32_t& py) {
        switch (t.orientation()) {
            case fi::Orientation::Portrait:
                px = y;
                py = ph - 1 - x;
                break;
            case fi::Orientation::PortraitInverted:
                px = pw - 1 - y;
                py = x;
                break;
            case fi::Orientation::LandscapeClockwise:
                px = pw - 1 - x;
                py = ph - 1 - y;
                break;
            default:
                px = x;
                py = y;
                break;
        }
    };
    int32_t ax, ay, bx, by;
    toPanel(x0, y0, ax, ay);
    toPanel(x1, y1, bx, by);
    const int32_t px0 = std::max<int32_t>(0, std::min(ax, bx)), px1 = std::min<int32_t>(pw - 1, std::max(ax, bx));
    const int32_t py0 = std::max<int32_t>(0, std::min(ay, by)), py1 = std::min<int32_t>(ph - 1, std::max(ay, by));
    if (px1 < px0 || py1 < py0) return true;
    const bool white = c == Color::White;
    for (int32_t py = py0; py <= py1; py++) paintSpan(fb + py * pwb, (int)px0, (int)px1, white);
    return true;
}

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

void setOrientation(fi::Orientation orientation) {
    fi::DisplayTarget& t = target();
    if (t.orientation() == orientation) return;
    t.setOrientation(orientation);
    const bool touch = g_device.hasTouch, buttons = g_device.hasButtons;
    g_device = t.deviceContext();
    g_device.hasTouch = touch;
    g_device.hasButtons = buttons;
}

fi::DisplayTarget& target() {
    if (!g_target) begin();
    return *g_target;
}

const fi::DeviceContext& device() {
    if (!g_target) begin();
    return g_device;
}

void touchToLogical(float nx, float ny, int16_t& x, int16_t& y) {
    const fi::Point p = fi::touchToLogical(device(), nx, ny);
    x = p.x;
    y = p.y;
}

int16_t width() { return device().width; }
int16_t height() { return device().height; }
Rect screen() { return device().screen(); }

void clear(Color color) { fill(screen(), color); }

void present(bool fullRefresh) { Display.update(fullRefresh ? REFRESH_FULL : REFRESH_PARTIAL); }

bool periodicFullRefresh() {
    static uint8_t n = 0;
    return (++n % 8) == 0;
}

Pass::Pass() : frame_(target(), device(), input_, g_hits) {
    g_backBand = Rect{};
    g_pager = ListPager{};
}

void setListPager(int count, int perPage) { g_pager = ListPager{count, perPage}; }
ListPager listPager() { return g_pager; }

bool backTapped(int16_t x, int16_t y) { return g_backBand.contains(x, y); }

bool touchOnly() { return Platform::kDevice.touchOnly; }

bool touchUi() { return Platform::kDevice.touch; }

namespace {
bool g_touchMissing = false;
}
void setTouchMissing(bool missing) { g_touchMissing = missing; }
bool touchMissing() { return g_touchMissing; }

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

void fill(Rect r, Color color, uint8_t radius) {
    if (r.empty()) return;
    if (radius == 0 && fastFill(r, color)) return;
    target().fill(r, solid(color), radius);
}

void frameRect(Rect r, Color color, uint8_t w, uint8_t radius) {
    if (radius == 0 && w > 0 && r.width > 2 * w && r.height > 2 * w &&
        (color == Color::Black || color == Color::White)) {
        // Four solid bands: the same pixels as stroke() with square corners.
        fill(Rect{r.x, r.y, r.width, w}, color);
        fill(Rect{r.x, (int16_t)(r.bottom() - w), r.width, w}, color);
        fill(Rect{r.x, (int16_t)(r.y + w), w, (int16_t)(r.height - 2 * w)}, color);
        fill(Rect{(int16_t)(r.right() - w), (int16_t)(r.y + w), w, (int16_t)(r.height - 2 * w)}, color);
        return;
    }
    target().stroke(r, solid(color), w, radius);
}

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
    b.percent =
            (uint8_t)(Battery.getPercentage() < 0 ? 0
                                                  : (Battery.getPercentage() > 100 ? 100 : Battery.getPercentage()));
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

namespace {
// The edge the front keys are at, as the screen is turned: 0 bottom (portrait),
// 1 top (portrait upside down), 2 right (landscape), 3 left (landscape upside down).
int keyEdge() {
    switch (target().orientation()) {
        case fi::Orientation::PortraitInverted:
            return 1;
        case fi::Orientation::LandscapeCounterClockwise:
            return 2;
        case fi::Orientation::LandscapeClockwise:
            return 3;
        default:
            return 0;
    }
}
int16_t keyTabHeight() { return (int16_t)(lineHeight(FontSmall) + 10); }

// The screen below the key tabs when the keys are at the top.
Rect screenUnderKeys() {
    Rect area = screen();
    if (!touchOnly() && keyEdge() == 1) takeTop(area, (int16_t)(keyTabHeight() + 4));
    return area;
}
}  // namespace

bool keysReversed() { return !touchOnly() && keyEdge() == 1; }

Rect topBar(Frame& f, const char* title) {
    Rect area = screenUnderKeys();
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 18);
    Rect bar = takeTop(area, h);
    fill(bar, Color::Black);
    Rect inner = insetX(bar, kMargin);
    const int16_t batW = 92;
    battery(f, Rect{(int16_t)(inner.right() - batW), inner.y, batW, inner.height}, Color::White);
    Rect titleR{inner.x, inner.y, (int16_t)(inner.width - batW - kGap), inner.height};
    if (Platform::kDevice.touch) {
        // Touch boards: the bar is the BACK control, so show it.
        text(Rect{titleR.x, titleR.y, 22, titleR.height}, "\xE2\x97\x80", FontBody, Color::White);  // U+25C0
        titleR.x = (int16_t)(titleR.x + 26);
        titleR.width = (int16_t)(titleR.width - 26);
    }
    text(titleR, title, FontTitle, Color::White);
    g_backBand = bar;
    return area;
}

Rect appHeader(Frame& f) {
    Rect area = screenUnderKeys();
    const int16_t h = (int16_t)(lineHeight(FontTitle) + 16);
    Rect bar = takeTop(area, h);
    Rect inner = insetX(bar, kMargin + 4);
    icon::book(inner.x, (int16_t)(inner.y + (inner.height - 20) / 2), Color::Black);
    const int16_t batW = 92;
    text(Rect{(int16_t)(inner.x + 28), inner.y, (int16_t)(inner.width - 28 - batW), inner.height}, "book orbit",
         FontTitle);
    battery(f, Rect{(int16_t)(inner.right() - batW), inner.y, batW, inner.height}, Color::Black);
    hline(0, width(), (int16_t)(bar.bottom() - 1));
    return area;
}

Rect footerHints(Frame&, Rect area, const char* line1, const char* line2, const char* touchLine) {
    if (touchOnly() && ((line1 && *line1) || (line2 && *line2))) {
        // The screens' hints name LEFT / RIGHT / OK / BACK, which this board lacks;
        // with the touch panel silent, the held keys stand in for them (C4).
        line1 = tr(g_touchMissing ? MsgId::TOUCH_FALLBACK_L1 : MsgId::TOUCH_HINT_L1);
        line2 = tr(g_touchMissing ? MsgId::TOUCH_FALLBACK_L2 : MsgId::TOUCH_HINT_L2);
        if (touchLine && !g_touchMissing) line2 = touchLine;   // the screen's own gesture (C9)
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

Rect keyHints(Frame& f, Rect area, const KeyLabels& keys, const char* info) {
    if (touchOnly())
        return footerHints(f, area, info ? info : "x", nullptr, keys.touch);   // replaced by the touch hints
    // The tabs stand at the keys (the device profile's centres along the bottom
    // edge) wherever the screen is turned: along the top (upside down: the keys
    // in the other order, LEFT and RIGHT swapped, see keysReversed()), or down a
    // side in landscape.
    const int16_t* kCentre = Platform::kDevice.keyCentres;
    const int edge = keyEdge();
    const char* labels[4] = {keys.back, keys.ok, edge == 1 ? keys.right : keys.left,
                             edge == 1 ? keys.left : keys.right};
    const int16_t W = width(), H = height();
    const int16_t tabH = keyTabHeight();
    if (edge == 0) takeBottom(area, (int16_t)(tabH + 8));
    int16_t infoH = 0;
    if (info && *info) infoH = (int16_t)(wrappedHeight(info, FontSmall, (int16_t)(W - 2 * (kMargin + 4)), 2) + 6);
    if (infoH) {
        Rect foot = takeBottom(area, (int16_t)(infoH + 8));
        hline(0, W, foot.y);
        text(Rect{(int16_t)(kMargin + 4), (int16_t)(foot.y + 4), (int16_t)(W - 2 * (kMargin + 4)), infoH}, info,
             FontSmall, Color::Black, TextAlign::Left, 2);
    }
    auto drawTab = [&](Rect tab, Rect label, const char* s) {
        fill(tab, Color::White, 5);
        frameRect(tab, Color::Black, 1, 5);
        if (s && *s) text(label, s, FontSmall, Color::Black, TextAlign::Center, 2);
    };
    if (edge <= 1) {
        const int16_t panelW = W;   // portrait: the keys run along the width
        const int16_t tabW = (int16_t)(Platform::kDevice.keyHintWidth * panelW / 480);
        for (int i = 0; i < 4; i++) {
            int16_t cx = (int16_t)(kCentre[i] * panelW / 480);
            if (edge == 1) cx = (int16_t)(panelW - cx);
            // Open at the edge: the tab runs off the screen, towards its key.
            Rect tab = edge == 0 ? Rect{(int16_t)(cx - tabW / 2), (int16_t)(H - tabH), tabW, (int16_t)(tabH + 6)}
                                 : Rect{(int16_t)(cx - tabW / 2), (int16_t)-6, tabW, (int16_t)(tabH + 6)};
            Rect label{(int16_t)(tab.x + 2), edge == 0 ? tab.y : (int16_t)0, (int16_t)(tab.width - 4), tabH};
            drawTab(tab, label, labels[i]);
        }
        return area;
    }
    // Landscape: the keys run down the right edge (bottom to top: BACK, OK, LEFT,
    // RIGHT) or, upside down, the left one (top to bottom).
    const int16_t panelH = H;   // the keys' axis is the screen's height now
    const int16_t tabW = 96, keyTabH = (int16_t)(Platform::kDevice.keyHintWidth * panelH / 480);
    Rect strip = edge == 2 ? Rect{(int16_t)(area.right() - tabW - 6), area.y, (int16_t)(tabW + 6), area.height}
                           : Rect{area.x, area.y, (int16_t)(tabW + 6), area.height};
    if (edge == 2)
        area.width = (int16_t)(area.width - strip.width);
    else {
        area.x = (int16_t)(area.x + strip.width);
        area.width = (int16_t)(area.width - strip.width);
    }
    for (int i = 0; i < 4; i++) {
        const int16_t at = (int16_t)(kCentre[i] * panelH / 480);
        const int16_t cy = edge == 2 ? (int16_t)(panelH - at) : at;
        Rect tab = edge == 2 ? Rect{(int16_t)(W - tabW), (int16_t)(cy - keyTabH / 2), (int16_t)(tabW + 6), keyTabH}
                             : Rect{(int16_t)-6, (int16_t)(cy - keyTabH / 2), (int16_t)(tabW + 6), keyTabH};
        Rect label{(int16_t)(edge == 2 ? tab.x + 4 : 4), tab.y, (int16_t)(tabW - 8), keyTabH};
        drawTab(tab, label, labels[i]);
    }
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

int16_t cardMinHeight() { return (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 8); }

CardList cardList(Rect area, int count, int selected, int16_t roomyH, int16_t roomyGap) {
    CardList l{roomyH, roomyGap, 0, count};
    if (count > 0 && count * (roomyH + roomyGap) > area.height) {
        l.gap = 4;
        l.cardH = (int16_t)std::max<int>(cardMinHeight(), area.height / count - l.gap);
    }
    l.perPage = std::max(1, (int)((area.height + l.gap) / (l.cardH + l.gap)));
    if (count > l.perPage) {
        if (selected < 0) selected = 0;
        l.first = (selected / l.perPage) * l.perPage;
        setListPager(count, l.perPage);
    }
    return l;
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
    const int16_t th = lineHeight(FontTitle);
    const int16_t sh = lineHeight(FontSmall);
    const bool hasSub = subtitle && *subtitle;
    // Roomy cards keep 8 px above the title; a tight one (a long list, a
    // landscape screen) shares what is left so the second line stays inside.
    const int16_t need = (int16_t)(th + (hasSub ? 4 + sh : 0));
    const int16_t padY = (int16_t)std::max(1, std::min(8, (r.height - need) / 2));
    Rect inner{(int16_t)(r.x + 12), (int16_t)(r.y + padY), (int16_t)(r.width - 24), (int16_t)(r.height - 2 * padY)};
    int16_t tagW = 0;
    if (tag && *tag) {
        tagW = pillWidth(tag);
        const int16_t ph = (int16_t)std::min<int>(sh + 6, inner.height);
        pill(Rect{(int16_t)(inner.right() - tagW), inner.y, tagW, ph}, tag, true, selected);
    }
    Rect titleR{inner.x, inner.y, (int16_t)(inner.width - tagW - (tagW ? kGap : 0)), th};
    text(titleR, title, FontTitle, ink);
    if (hasSub) {
        const int16_t sy = (int16_t)(inner.y + th + (r.height >= need + 16 ? 4 : 2));
        Rect sub{inner.x, sy, inner.width, (int16_t)std::max<int>(sh, inner.bottom() - sy)};
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

const char* deviceDisplayName() { return Platform::kDevice.name; }

// ---------------------------------------------------------------------------
namespace icon {

void book(int16_t x, int16_t y, Color c) {
    // Open book icon (20x20).
    static const uint8_t kBookBits[60] = {
            0x00, 0x00, 0x00,  // row  0
            0x00, 0x00, 0x00,  // row  1
            0x3E, 0x07, 0xC0,  // row  2: ..#####......#####..
            0x41, 0x98, 0x20,  // row  3: .#.....##..##.....#.
            0x80, 0x60, 0x10,  // row  4: #........##........#
            0x80, 0x60, 0x10,  // row  5: #........##........#
            0x9F, 0x6F, 0x90,  // row  6: #..#####.##.#####..#
            0x80, 0x60, 0x10,  // row  7: #........##........#
            0x80, 0x60, 0x10,  // row  8: #........##........#
            0x9F, 0x6F, 0x90,  // row  9: #..#####.##.#####..#
            0x80, 0x60, 0x10,  // row 10: #........##........#
            0x80, 0x60, 0x10,  // row 11: #........##........#
            0x9E, 0x67, 0x90,  // row 12: #..####..##..####..#
            0x80, 0x60, 0x10,  // row 13: #........##........#
            0x80, 0x60, 0x10,  // row 14: #........##........#
            0x7E, 0x67, 0xE0,  // row 15: .######..##..######.
            0x01, 0xF8, 0x00,  // row 16: .......######.......
            0x00, 0x60, 0x00,  // row 17: .........##.........
            0x00, 0x00, 0x00,  // row 18
            0x00, 0x00, 0x00,  // row 19
    };
    target().bitmap(Rect{x, y, 20, 20}, fi::BitmapRef{kBookBits, 20, 20, fi::BitmapFormat::BW1}, fi::BitmapMode::Center,
                    solid(c));
}

void logo(int16_t x, int16_t y, Color c) { book(x, y, c); }

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
        if (b < bars)
            fill(r, c);
        else
            frameRect(r, c);
    }
}

}  // namespace icon

}  // namespace uikit
