#include "orientation.h"

#include "../hal/display.h"
#include "../reader/reader.h"
#include "../ui/kit/ui_kit.h"

freeink::ui::Orientation uiOrientationOf(int orientation) {
    using freeink::ui::Orientation;
    static const Orientation kUi[4] = {Orientation::Portrait, Orientation::LandscapeCounterClockwise,
                                       Orientation::PortraitInverted, Orientation::LandscapeClockwise};
    return kUi[orientation & 3];
}

void applyOrientation(int ui, int book) {
    ui &= 3;
    // The interface the same way three times over: Adafruit GFX (pictures drawn
    // through the display) and FreeInkUI (the screens); the book's frames by both.
    static const uint8_t kGfx[4] = {3, 0, 1, 2};
    Display.setRotation(kGfx[ui]);
    uikit::setOrientation(uiOrientationOf(ui));
    Reader.setOrientation((ui + book) & 3);
}

namespace {
// Logical point of a screen turned `o` <-> the panel's own (as DisplayTarget).
void toPanel(int o, int32_t x, int32_t y, int32_t& px, int32_t& py) {
    const int32_t W = EPD_WIDTH, H = EPD_HEIGHT;
    switch (o & 3) {
        case 0: px = y; py = H - 1 - x; break;            // portrait: 90 CW
        case 1: px = x; py = y; break;                    // native
        case 2: px = W - 1 - y; py = x; break;            // 90 CCW
        default: px = W - 1 - x; py = H - 1 - y; break;  // 180
    }
}
void fromPanel(int o, int32_t px, int32_t py, int32_t& x, int32_t& y) {
    const int32_t W = EPD_WIDTH, H = EPD_HEIGHT;
    switch (o & 3) {
        case 0: x = H - 1 - py; y = px; break;
        case 1: x = px; y = py; break;
        case 2: x = py; y = W - 1 - px; break;
        default: x = W - 1 - px; y = H - 1 - py; break;
    }
}
}  // namespace

void interfaceToPage(int ui, int page, int16_t x, int16_t y, int16_t& px, int16_t& py) {
    int32_t qx, qy, rx, ry;
    toPanel(ui, x, y, qx, qy);
    fromPanel(page, qx, qy, rx, ry);
    px = (int16_t)rx;
    py = (int16_t)ry;
}
