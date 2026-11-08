#pragma once

// UiKit: Book Orbit's thin layer over FreeInkUI.
//
// Every screen draws through one freeink::ui::DisplayTarget that renders straight
// into FreeInkDisplay's framebuffer (the same buffer Adafruit_GFX/U8g2 still use
// for book pages and covers), in Portrait orientation, which matches the GFX
// rotation used by EPDDisplay. Layout is computed from the logical screen size,
// so the same code serves 480x800 (X4, X4 Classic, X4 Pro, Picco) and 528x792 (X3).
//
// Physical buttons are still handled by the per-screen handlers in
// src/app/screens; the hit areas registered here are what touch boards will
// route through later.

// The simulator's Arduino.h stub defines min/max as macros (the real core 3.x
// does not); FreeInkUI's headers declare functions with those names.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>
#pragma pop_macro("max")
#pragma pop_macro("min")

namespace uikit {

namespace fi = freeink::ui;

// Font slots bound in begin() (see tools/gen_ui_fonts.py for the faces/sizes).
enum : fi::FontId {
    FontSmall = 0,  // hints, tags, secondary lines
    FontBody = 1,   // list rows, body text
    FontTitle = 2,  // bold: headers, row titles
    FontLarge = 3,  // bold: sleep screen, big numbers
};

constexpr int16_t kMargin = 16;   // outer side margin
constexpr int16_t kGap = 8;       // gap between stacked rows
constexpr size_t kMaxHits = 96;   // hit areas per render pass

using Frame = fi::Frame<kMaxHits>;
using fi::Color;
using fi::Rect;
using fi::TextAlign;

// Binds the framebuffer and fonts. Call after Display.begin().
void begin();
bool ready();

fi::DisplayTarget& target();
const fi::DeviceContext& device();
int16_t width();
int16_t height();
Rect screen();

// Whole-screen fill (white unless told otherwise).
void clear(Color color = Color::White);
// Pushes the framebuffer to the panel: full refresh (clears ghosting) or fast.
void present(bool fullRefresh);
// For moving between list screens (catalog folders): false (fast refresh)
// except every 8th call, which clears the e-ink ghosting with a full one.
bool periodicFullRefresh();

// One render pass: a Frame over the shared target with no input routed.
class Pass {
public:
    Pass();
    Frame& frame() { return frame_; }
    fi::DrawTarget& t() { return frame_.target(); }

private:
    fi::InputSnapshot input_{};
    Frame frame_;
};

// Hit areas of the last render pass (what touch routes through). Returns the
// topmost area containing (x, y). Note: a modal drawn after a screen starts a
// new pass, so while it is up the screen's areas are gone.
bool hitTest(int16_t x, int16_t y, fi::ActionId& action, int16_t& value);
// True when (x, y) is on the title bar of the last screen drawn with topBar():
// touch boards without a BACK key (Picco) go back by tapping it.
bool backTapped(int16_t x, int16_t y);
// True on boards driven by touch (dialogs then show tappable buttons).
bool touchUi();
// True on boards whose only keys are UP / DOWN / POWER (Picco): every
// screen's key hints are replaced by touch hints.
bool touchOnly();

// ---- text ------------------------------------------------------------------
fi::TextStyle style(fi::FontId font, Color color = Color::Black, TextAlign align = TextAlign::Left,
                    uint8_t maxLines = 1);
int16_t lineHeight(fi::FontId font);
int16_t textWidth(fi::FontId font, const char* text);
// Draws text into rect (wrapping up to maxLines, ellipsis on overflow, vertically centered).
void text(Rect rect, const char* str, fi::FontId font, Color color = Color::Black,
          TextAlign align = TextAlign::Left, uint8_t maxLines = 1);
// Height of `str` wrapped into `width` (at most maxLines lines).
int16_t wrappedHeight(const char* str, fi::FontId font, int16_t width, uint8_t maxLines);

// ---- geometry helpers --------------------------------------------------------
Rect takeTop(Rect& area, int16_t h);
Rect takeBottom(Rect& area, int16_t h);
Rect insetX(Rect r, int16_t dx);
Rect insetXY(Rect r, int16_t dx, int16_t dy);

// ---- primitives ----------------------------------------------------------------
void fill(Rect r, Color color, uint8_t radius = 0);
void frameRect(Rect r, Color color, uint8_t width = 1, uint8_t radius = 0);
void hline(int16_t x0, int16_t x1, int16_t y, Color color = Color::Black, uint8_t width = 1);
void vline(int16_t x, int16_t y0, int16_t y1, Color color = Color::Black, uint8_t width = 1);

// ---- Book Orbit chrome ---------------------------------------------------------
// Black title band with the battery on the right. Returns the area below it.
Rect topBar(Frame& f, const char* title);
// Home header: logo, "book orbit", battery, divider. Returns the area below it.
Rect appHeader(Frame& f);
// Divider plus up to two hint lines at the bottom of `area`. Returns the area above.
Rect footerHints(Frame& f, Rect area, const char* line1, const char* line2 = nullptr);
// Small rounded label. dark = black fill/white text; otherwise outlined.
// onInverted: drawn on a black row (colors swap).
void pill(Rect r, const char* label, bool dark, bool onInverted);
// Width a pill needs for `label`.
int16_t pillWidth(const char* label);
// A settings-style card: bold title, secondary line, optional tag pill.
void card(Frame& f, Rect r, const char* title, const char* subtitle, const char* tag, bool selected,
          fi::ActionId action = fi::NO_ACTION, int16_t value = 0);
// Battery glyph + percentage, right-aligned in rect.
void battery(Frame& f, Rect r, Color color);
// Progress bar with outline (value 0..1000).
void progress(Frame& f, Rect r, int32_t permille);
// Modal panel centered on screen: shadow, double border, black title band.
// Returns the body rect under the title band.
Rect modalPanel(const char* title, int16_t w, int16_t h);

// Model name for the sleep screen and device info (from the build's device flag).
const char* deviceDisplayName();

// ---- icons (DrawTarget primitives, so they follow the logical frame) --------------
namespace icon {
void book(int16_t x, int16_t y, Color c);         // 20x20
void logo(int16_t x, int16_t y, Color c);         // legacy alias -> book
void folder(int16_t x, int16_t y, Color c);       // 14x11
void check(int16_t x, int16_t y, Color c);        // 10x10
void cloud(int16_t x, int16_t y, Color c);        // 14x10
void sync(int16_t x, int16_t y, Color c);         // 20x20
void signal(int16_t x, int16_t y, int bars, Color c);  // 22x20, bars 1..4
}  // namespace icon

}  // namespace uikit
