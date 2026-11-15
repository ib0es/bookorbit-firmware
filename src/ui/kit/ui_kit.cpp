#include "ui_kit.h"
#include "../i18n.h"

#include "../../platform/device.h"

#include <algorithm>
#include <new>
#include <stdio.h>
#include <string.h>

#include <WiFi.h>
#include "../../platform/hal/battery.h"
#include "../../platform/hal/display.h"
#include "../../services/sync_clock.h"
#include "ui_fonts.h"
#include "ui_icons.h"

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

size_t hitCount() { return g_hits.count(); }

bool hitAt(size_t i, Rect& rect, fi::ActionId& action, int16_t& value) {
    if (i >= g_hits.count()) return false;
    const fi::Interaction& it = g_hits.data()[i];
    rect = it.rect;
    action = it.action;
    value = it.value;
    return true;
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
    fill(Rect{x0, y, (int16_t)(x1 - x0), (int16_t)w}, color);
}

void vline(int16_t x, int16_t y0, int16_t y1, Color color, uint8_t w) {
    fill(Rect{x, y0, (int16_t)w, (int16_t)(y1 - y0)}, color);
}

// ---------------------------------------------------------------------------
namespace {
int g_utcOffsetMin = 0;
int16_t batteryPercent() {
    const int p = Battery.getPercentage();
    return (int16_t)(p < 0 ? 0 : (p > 100 ? 100 : p));
}
}  // namespace

void setClockOffset(int utcOffsetMin) { g_utcOffsetMin = utcOffsetMin; }

void batteryGlyph(int16_t x, int16_t y, Color c) {
    // Body 23x13 with a 3x5 nub, the charge as a filled part inside.
    Rect body{x, y, 23, 13};
    frameRect(body, c, 2, 3);
    fill(Rect{(int16_t)(x + 23), (int16_t)(y + 4), 3, 5}, c, 1);
    const int16_t inner = 23 - 8;
    const int16_t w = (int16_t)((inner * batteryPercent() + 50) / 100);
    if (w > 0) fill(Rect{(int16_t)(x + 4), (int16_t)(y + 4), w, 5}, c);
    if (Battery.isCharging()) {
        // A small bolt over the body (in the paper colour on the filled part).
        const Color bolt = inverse(c);
        const int16_t bx = (int16_t)(x + 9), by = (int16_t)(y + 2);
        target().triangle(fi::Point{(int16_t)(bx + 4), by}, fi::Point{bx, (int16_t)(by + 5)},
                          fi::Point{(int16_t)(bx + 4), (int16_t)(by + 5)}, solid(bolt));
        target().triangle(fi::Point{(int16_t)(bx + 1), (int16_t)(by + 4)},
                          fi::Point{(int16_t)(bx + 5), (int16_t)(by + 4)},
                          fi::Point{(int16_t)(bx + 1), (int16_t)(by + 9)}, solid(bolt));
    }
}

void battery(Frame& f, Rect r, Color color) {
    (void)f;
    char label[12];
    snprintf(label, sizeof(label), "%d%%", (int)batteryPercent());
    const int16_t glyphW = 27;
    batteryGlyph((int16_t)(r.right() - glyphW), (int16_t)(r.y + (r.height - 13) / 2), color);
    Rect lbl{r.x, r.y, (int16_t)(r.width - glyphW - 6), r.height};
    text(lbl, label, FontSmall, color, TextAlign::Right);
}

int16_t statusBarHeight() { return (int16_t)(lineHeight(FontSmall) + 6); }

void statusBar(Frame& f, Rect bar) {
    (void)f;
    Rect inner = insetX(bar, (int16_t)(kMargin + 6));
    char clk[8];
    if (SyncClock::isSet() && SyncClock::formatLocalTime(g_utcOffsetMin, clk, sizeof(clk)))
        text(Rect{inner.x, inner.y, 80, inner.height}, clk, FontSmall);
    int16_t x = (int16_t)(inner.right() - 27);
    batteryGlyph(x, (int16_t)(inner.y + (inner.height - 13) / 2), Color::Black);
    if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED) {
        x = (int16_t)(x - 22 - 12);
        icon::wifi(x, (int16_t)(inner.y + (inner.height - 16) / 2), Color::Black);
    }
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
    Rect sb = takeTop(area, statusBarHeight());
    statusBar(f, sb);
    Rect band = takeTop(area, (int16_t)(lineHeight(FontTitle) + 10));
    // The title centred, with the same room kept on both sides.
    const int16_t side = (int16_t)(kMargin + 30);
    Rect titleR{(int16_t)(band.x + side), band.y, (int16_t)(band.width - 2 * side), (int16_t)(band.height - 4)};
    if (Platform::kDevice.touch) {
        // Touch boards: the bar is the BACK control, so show it.
        text(Rect{(int16_t)(band.x + kMargin), titleR.y, 28, titleR.height}, "\xE2\x80\xB9", FontLarge);  // U+2039
    }
    text(titleR, title, FontTitle, Color::Black, TextAlign::Center);
    hline(0, width(), (int16_t)(band.bottom() - 2), Color::Black, 2);
    g_backBand = Rect{sb.x, sb.y, sb.width, (int16_t)(band.bottom() - sb.y)};
    takeTop(area, 6);
    return area;
}

Rect appHeader(Frame& f) {
    Rect area = screenUnderKeys();
    Rect bar = takeTop(area, statusBarHeight());
    statusBar(f, bar);
    if (!Platform::kDevice.hasPowerButton && Platform::kDevice.touch) {
        // No POWER key: a sleep button in the middle of the status line.
        const int16_t pwrW = 40, pwrH = (int16_t)(bar.height - 10);
        Rect pwrBtn{(int16_t)((bar.width - pwrW) / 2), (int16_t)(bar.y + 5), pwrW, pwrH};
        frameRect(pwrBtn, Color::Black, 1, kRadiusSmall);
        text(pwrBtn, "Zz", FontSmall, Color::Black, TextAlign::Center);
        f.hit(pwrBtn, 1003, 0);  // ActionSleep -> BTN_POWER
    }
    takeTop(area, 4);
    return area;
}

Rect footerHints(Frame& f, Rect area, const char* line1, const char* line2, const char* touchLine) {
    // Touch boards without UP / DOWN keys get them on screen, in the footer.
    const bool keyButtons = !Platform::kDevice.hasUpDown && Platform::kDevice.touch && !g_touchMissing;
    if (touchOnly() && ((line1 && *line1) || (line2 && *line2))) {
        // The screens' hints name LEFT / RIGHT / OK / BACK, which this board lacks.
        // With the touch panel silent, the held keys stand in for them (C4): said
        // in full. With touch, the general gestures go without saying (the "‹" in
        // the title shows the way back); only a screen's own gesture (C9) is said,
        // in one line.
        if (g_touchMissing) {
            line1 = tr(MsgId::TOUCH_FALLBACK_L1);
            line2 = tr(MsgId::TOUCH_FALLBACK_L2);
        } else {
            line1 = touchLine;
            line2 = nullptr;
            if ((!line1 || !*line1) && !keyButtons) return area;
        }
    }
    const int16_t innerW = (int16_t)(width() - 2 * (kMargin + 4));
    const int16_t h1 = (line1 && *line1) ? (int16_t)(wrappedHeight(line1, FontSmall, innerW, 2) + 4) : 0;
    const int16_t h2 = (line2 && *line2) ? (int16_t)(wrappedHeight(line2, FontSmall, innerW, 2) + 4) : 0;
    if (h1 + h2 == 0 && !keyButtons) return area;
    Rect foot = takeBottom(area, (int16_t)std::max<int>(h1 + h2 + 14, keyButtons ? 28 + 16 : 0));
    hline(0, width(), foot.y);
    Rect inner = insetXY(foot, kMargin + 4, 8);
    if (keyButtons) {
        const int16_t btnSize = 28;
        Rect btnDown{(int16_t)(inner.right() - btnSize), (int16_t)(inner.y + (inner.height - btnSize) / 2), btnSize,
                     btnSize};
        Rect btnUp{(int16_t)(btnDown.x - btnSize - 6), btnDown.y, btnSize, btnSize};
        frameRect(btnUp, Color::Black, 1, kRadiusSmall);
        text(btnUp, "\xE2\x96\xB2", FontSmall, Color::Black, TextAlign::Center);
        f.hit(btnUp, 1001, 0);  // ActionPagePrev -> BTN_UP
        frameRect(btnDown, Color::Black, 1, kRadiusSmall);
        text(btnDown, "\xE2\x96\xBC", FontSmall, Color::Black, TextAlign::Center);
        f.hit(btnDown, 1002, 0);  // ActionPageNext -> BTN_DOWN
        inner.width = (int16_t)(inner.width - 2 * btnSize - 12);
    }
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
        fill(tab, Color::White, kRadius);
        frameRect(tab, Color::Black, 1, kRadius);
        if (s && *s) text(label, s, FontSmall, Color::Black, TextAlign::Center, 2);
    };
    if (edge <= 1) {
        const int16_t panelW = W;   // portrait: the keys run along the width
        int16_t tabW = (int16_t)(Platform::kDevice.keyHintWidth * panelW / 480);
        // A label wider than the tab widens all four, as far as the keys' spacing allows.
        int16_t spacing = 32767;
        for (int i = 1; i < 4; i++)
            spacing = std::min<int16_t>(spacing, (int16_t)((kCentre[i] - kCentre[i - 1]) * panelW / 480));
        int16_t need = 0;
        for (const char* l : labels)
            if (l && *l) need = std::max<int16_t>(need, (int16_t)(textWidth(FontSmall, l) + 10));
        if (need > tabW) tabW = std::min<int16_t>(need, (int16_t)(spacing - 4));
        for (int i = 0; i < 4; i++) {
            int16_t cx = (int16_t)(kCentre[i] * panelW / 480);
            if (edge == 1) cx = (int16_t)(panelW - cx);
            // Open at the edge: the tab runs off the screen, towards its key.
            Rect tab = edge == 0 ? Rect{(int16_t)(cx - tabW / 2), (int16_t)(H - tabH), tabW, (int16_t)(tabH + 6)}
                                 : Rect{(int16_t)(cx - tabW / 2), (int16_t)-6, tabW, (int16_t)(tabH + 6)};
            Rect label{(int16_t)(tab.x + 1), edge == 0 ? tab.y : (int16_t)0, (int16_t)(tab.width - 2), tabH};
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
        fill(r, ink, kRadiusSmall);
        text(r, label, FontSmall, paper, TextAlign::Center);
    } else {
        frameRect(r, ink, 1, kRadiusSmall);
        text(r, label, FontSmall, ink, TextAlign::Center);
    }
}

int16_t cardMinHeight() { return (int16_t)(lineHeight(FontBody) + lineHeight(FontSmall) + 6); }

int16_t rowHeight(bool twoLines) {
    return twoLines ? (int16_t)(lineHeight(FontBody) + lineHeight(FontSmall) + 14)
                    : (int16_t)(lineHeight(FontBody) + 24);
}

int16_t sectionHeight() { return (int16_t)(lineHeight(FontSmall) + 12); }

void section(Rect r, const char* caption) {
    if (!caption || !*caption) return;
    text(Rect{(int16_t)(r.x + 14), (int16_t)(r.y + 6), (int16_t)(r.width - 28), (int16_t)(r.height - 6)}, caption,
         FontSmall);
}

void selectionFrame(Rect r) { frameRect(r, Color::Black, 3, kRadius); }

void toggle(Rect r, bool on) {
    const int16_t w = 46, h = 26;
    Rect sw{(int16_t)(r.x + (r.width - w) / 2), (int16_t)(r.y + (r.height - h) / 2), w, h};
    const int16_t knob = (int16_t)(h - 10);
    if (on) {
        fill(sw, Color::Black, (uint8_t)(h / 2));
        fill(Rect{(int16_t)(sw.right() - 5 - knob), (int16_t)(sw.y + 5), knob, knob}, Color::White,
             (uint8_t)(knob / 2));
    } else {
        frameRect(sw, Color::Black, 2, (uint8_t)(h / 2));
        fill(Rect{(int16_t)(sw.x + 5), (int16_t)(sw.y + 5), knob, knob}, Color::Black, (uint8_t)(knob / 2));
    }
}

void scrollbar(Rect track, int first, int perPage, int count) {
    if (count <= perPage || count <= 0 || track.height < 8) return;
    vline((int16_t)(track.x + track.width / 2), track.y, track.bottom(), Color::Black, 1);
    int32_t thumbH = (int32_t)track.height * perPage / count;
    if (thumbH < 16) thumbH = 16;
    const int32_t maxFirst = count - perPage;
    const int32_t y = track.y + (int32_t)(track.height - thumbH) * (first > maxFirst ? maxFirst : first) / maxFirst;
    fill(Rect{(int16_t)(track.x + track.width / 2 - 2), (int16_t)y, 5, (int16_t)thumbH}, Color::Black, 2);
}

void row(Frame& f, Rect r, const char* title, const char* subtitle, const RowOpts& o, bool selected,
         fi::ActionId action, int16_t value) {
    fill(r, Color::White);
    if (selected) selectionFrame(r);
    if (action != fi::NO_ACTION) f.hit(r, action, value);
    Rect in = insetXY(r, 16, 3);
    const int16_t lb = lineHeight(FontBody), ls = lineHeight(FontSmall);

    // Right side, from the edge inwards: chevron, switch / check, badge, value.
    int16_t right = in.right();
    if (o.chevron) {
        const int16_t cw = 14;
        text(Rect{(int16_t)(right - cw), r.y, cw, r.height}, "\xE2\x80\xBA", FontBody, Color::Black,
             TextAlign::Right);  // U+203A
        right = (int16_t)(right - cw - 8);
    }
    if (o.toggle >= 0) {
        toggle(Rect{(int16_t)(right - 46), r.y, 46, r.height}, o.toggle > 0);
        right = (int16_t)(right - 46 - 10);
    } else if (o.checked) {
        icon::check((int16_t)(right - 14), (int16_t)(r.y + (r.height - 10) / 2), Color::Black);
        right = (int16_t)(right - 14 - 12);
    }
    if (o.badge && *o.badge) {
        const int16_t bw = pillWidth(o.badge), bh = (int16_t)(ls + 4);
        pill(Rect{(int16_t)(right - bw), (int16_t)(r.y + (r.height - bh) / 2), bw, bh}, o.badge, false, false);
        right = (int16_t)(right - bw - 8);
    }
    if (o.value && *o.value) {
        // The title keeps its room; the value takes what is left, at least 30 %
        // and at most 55 % of the row (cut with an ellipsis beyond that).
        const int16_t avail = (int16_t)(right - in.x);
        const int16_t titleW = textWidth(FontBody, title);
        int16_t maxW = (int16_t)(avail - titleW - 16);
        if (maxW < avail * 30 / 100) maxW = (int16_t)(avail * 30 / 100);
        if (maxW > avail * 55 / 100) maxW = (int16_t)(avail * 55 / 100);
        int16_t vw = (int16_t)(textWidth(FontSmall, o.value) + 2);
        if (vw > maxW) vw = maxW;
        text(Rect{(int16_t)(right - vw), r.y, vw, r.height}, o.value, FontSmall, Color::Black, TextAlign::Right);
        right = (int16_t)(right - vw - 12);
    }

    Rect left{in.x, in.y, (int16_t)(right - in.x), in.height};
    const bool sub = subtitle && *subtitle && left.height >= lb + ls - 2;
    if (!sub) {
        text(left, title, FontBody);
        return;
    }
    const int16_t top = (int16_t)(left.y + (left.height - lb - ls + 2) / 2);
    text(Rect{left.x, top, left.width, lb}, title, FontBody);
    text(Rect{left.x, (int16_t)(top + lb - 2), left.width, ls}, subtitle, FontSmall);
}

CardList cardList(Rect area, int count, int selected, int16_t roomyH, int16_t roomyGap) {
    CardList l{roomyH, roomyGap, 0, count};
    if (count > 0 && count * (roomyH + roomyGap) > area.height) {
        l.gap = 2;
        l.cardH = (int16_t)std::max<int>(cardMinHeight(), area.height / count - l.gap);
        if (l.cardH > roomyH) l.cardH = roomyH;
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
    (void)tag;
    row(f, r, title, subtitle, RowOpts{}, selected, action, value);
}

void progress(Frame& f, Rect r, int32_t permille) {
    (void)f;
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    // A thin rule with the part read drawn thick over it.
    const int16_t barH = (int16_t)std::min<int>(5, r.height);
    const int16_t y = (int16_t)(r.y + (r.height - barH) / 2);
    hline(r.x, r.right(), (int16_t)(y + barH / 2), Color::Black, 1);
    const int16_t w = (int16_t)((int32_t)r.width * permille / 1000);
    if (w > 0) fill(Rect{r.x, y, w, barH}, Color::Black);
}

void chapterBar(Rect r, int done, int total) {
    // Solid, without notches (the book's bar has one at each chapter).
    if (total <= 0) return;
    if (done < 0) done = 0;
    if (done > total) done = total;
    hline(r.x, r.right(), (int16_t)(r.y + r.height / 2), Color::Black, 1);
    const int16_t w = (int16_t)((int32_t)r.width * done / total);
    if (w > 0) fill(Rect{r.x, r.y, w, r.height}, Color::Black);
}

void bookBar(Rect r, int32_t permille, const uint16_t* ticks, int count) {
    if (permille < 0) permille = 0;
    if (permille > 1000) permille = 1000;
    const int16_t h = r.height;
    const int16_t midY = (int16_t)(r.y + h / 2);
    hline(r.x, r.right(), midY, Color::Black, 1);
    const int16_t w = (int16_t)((int32_t)r.width * permille / 1000);
    if (w > 0) fill(Rect{r.x, r.y, w, h}, Color::Black);
    // Chapter starts: a white cut in the part read, a notch above and below the line elsewhere.
    for (int i = 0; i < count; i++) {
        if (ticks[i] == 0 || ticks[i] >= 1000) continue;
        const int16_t x = (int16_t)(r.x + (int32_t)r.width * ticks[i] / 1000);
        if (x < r.x + w)
            vline(x, r.y, (int16_t)(r.y + h), Color::White, 1);
        else
            vline(x, (int16_t)(r.y - 1), (int16_t)(r.y + h + 1), Color::Black, 1);
    }
}

Rect modalPanel(const char* title, int16_t w, int16_t h) {
    if (w > width() - 2 * kMargin) w = (int16_t)(width() - 2 * kMargin);
    if (h > height() - 2 * kMargin) h = (int16_t)(height() - 2 * kMargin);
    Rect box{(int16_t)((width() - w) / 2), (int16_t)((height() - h) / 2), w, h};
    // A white margin round the panel keeps it apart from the screen under it.
    fill(insetXY(box, -6, -6), Color::White, (uint8_t)(kRadius + 6));
    fill(box, Color::White, kRadius);
    frameRect(box, Color::Black, 2, kRadius);
    Rect area = box.inset(fi::Insets{2, 2, 2, 2});
    const int16_t bandH = (int16_t)(lineHeight(FontTitle) + 14);
    Rect band = takeTop(area, bandH);
    text(insetX(band, 18), title, FontTitle, Color::Black, TextAlign::Center);
    hline((int16_t)(box.x + 16), (int16_t)(box.right() - 16), (int16_t)(band.bottom() - 1));
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

void wifi(int16_t x, int16_t y, Color c) {
    // A quarter fan: two arcs and a dot, cut to 90 degrees.
    fi::DisplayTarget& t = target();
    const Rect oldClip = t.clipRect();
    const int16_t cx = (int16_t)(x + 11), cy = (int16_t)(y + 16);
    t.setClipRect(Rect{x, y, 22, (int16_t)(16 - 1)});
    t.stroke(Rect{(int16_t)(cx - 15), (int16_t)(cy - 15), 30, 30}, solid(c), 2, 15);
    t.stroke(Rect{(int16_t)(cx - 9), (int16_t)(cy - 9), 18, 18}, solid(c), 2, 9);
    t.setClipRect(oldClip);
    const Color paper = inverse(c);
    t.triangle(fi::Point{cx, cy}, fi::Point{(int16_t)(cx - 17), (int16_t)(cy - 17)},
               fi::Point{(int16_t)(cx - 17), (int16_t)(cy + 1)}, solid(paper));
    t.triangle(fi::Point{cx, cy}, fi::Point{(int16_t)(cx + 17), (int16_t)(cy - 17)},
               fi::Point{(int16_t)(cx + 17), (int16_t)(cy + 1)}, solid(paper));
    fill(Rect{(int16_t)(cx - 2), (int16_t)(cy - 4), 5, 5}, c, 2);
}

namespace {
// Scale v (in a 48-unit design grid) to the box.
inline int16_t sc(Rect b, int v) { return (int16_t)(v * std::min(b.width, b.height) / 48); }
inline fi::Point pt(Rect b, int x, int y) {
    const int16_t s = (int16_t)std::min(b.width, b.height);
    const int16_t ox = (int16_t)(b.x + (b.width - s) / 2), oy = (int16_t)(b.y + (b.height - s) / 2);
    return fi::Point{(int16_t)(ox + x * s / 48), (int16_t)(oy + y * s / 48)};
}
inline Rect rc(Rect b, int x, int y, int w, int h) {
    const fi::Point p = pt(b, x, y);
    return Rect{p.x, p.y, sc(b, w), sc(b, h)};
}
}  // namespace

// A Material Symbols icon (ui_icons.h): the largest of its sizes that fits, centred.
void bitmapIcon(Rect b, const icons::IconSize* set, Color c) {
    const icons::IconSize* pick = &set[0];
    for (int i = 0; i < icons::kIconSizeCount; i++)
        if (set[i].size <= std::min(b.width, b.height)) pick = &set[i];
    const int16_t s = pick->size;
    target().bitmap(Rect{(int16_t)(b.x + (b.width - s) / 2), (int16_t)(b.y + (b.height - s) / 2), s, s},
                    fi::BitmapRef{pick->bits, s, s, fi::BitmapFormat::BW1}, fi::BitmapMode::Center, solid(c));
}

void folderLarge(Rect b, Color c) {
    const uint8_t w = (uint8_t)std::max<int>(2, sc(b, 3));
    // Tab, then the body.
    frameRect(rc(b, 4, 10, 18, 10), c, w, (uint8_t)sc(b, 3));
    fill(rc(b, 5, 16, 38, 8), inverse(c));
    frameRect(rc(b, 4, 15, 40, 26), c, w, (uint8_t)sc(b, 4));
}

void syncLarge(Rect b, Color c) { bitmapIcon(b, icons::kSync, c); }

void menuBook(Rect b, Color c) { bitmapIcon(b, icons::kMenuBook, c); }

void cloudOutline(Rect b, Color c) { bitmapIcon(b, icons::kCloud, c); }

void settings(Rect b, Color c) { bitmapIcon(b, icons::kSettings, c); }

void cloudLarge(Rect b, Color c) {
    // Two bumps over a flat base, outlined.
    const uint8_t w = (uint8_t)std::max<int>(2, sc(b, 3));
    fill(rc(b, 12, 12, 20, 20), c, (uint8_t)sc(b, 10));
    fill(rc(b, 24, 18, 16, 16), c, (uint8_t)sc(b, 8));
    fill(rc(b, 6, 22, 38, 16), c, (uint8_t)sc(b, 8));
    const int16_t k = w;
    Rect a1 = rc(b, 12, 12, 20, 20), a2 = rc(b, 24, 18, 16, 16), a3 = rc(b, 6, 22, 38, 16);
    fill(insetXY(a1, k, k), inverse(c), (uint8_t)std::max(0, sc(b, 10) - k));
    fill(insetXY(a2, k, k), inverse(c), (uint8_t)std::max(0, sc(b, 8) - k));
    fill(insetXY(a3, k, k), inverse(c), (uint8_t)std::max(0, sc(b, 8) - k));
}

void bookOutline(Rect b, Color c) {
    // A book seen from the front: cover, the page block below, a letter on it.
    const uint8_t w = 2;
    frameRect(rc(b, 10, 4, 28, 34), c, w, 2);
    frameRect(rc(b, 10, 36, 28, 8), c, w, 2);
    target().line(pt(b, 17, 28), pt(b, 24, 11), w, solid(c));
    target().line(pt(b, 24, 11), pt(b, 31, 28), w, solid(c));
    target().line(pt(b, 20, 22), pt(b, 28, 22), w, solid(c));
}

}  // namespace icon

}  // namespace uikit
