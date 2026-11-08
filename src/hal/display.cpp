#include "display.h"
#include "config.h"

EPDDisplay Display;

EPDDisplay::EPDDisplay() 
    : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT), _eink(nullptr) {}

EPDDisplay::~EPDDisplay() {
    if (_eink) {
        delete _eink;
        _eink = nullptr;
    }
}

bool EPDDisplay::begin() {
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
    // X3 build: confirm the board (I2C fingerprint) and pick the UC8253 or
    // UC8279 controller profile, then switch the facade to the 792x528 panel.
    const bool isX3 = freeink::selectXteinkDevice();
    if (!isX3) {
        Serial.println("[EPD] WARNING: this is an X3 build but the board did not identify as an X3!");
    }
#else
    // 1. Detect and apply the display controller for this unit's batch (SSD1677, UC8179, UC8279)
    freeink::applyXteinkDisplayController();
#endif

    // 2. Initialize FreeInk display facade
    if (!_eink) {
        _eink = new freeink::FreeInkDisplay(EPD_SCK_PIN, EPD_MOSI_PIN, EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN);
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
        _eink->setDisplayX3();
#endif
    }
    _eink->begin();
    if (_eink->getDisplayWidth() != EPD_WIDTH || _eink->getDisplayHeight() != EPD_HEIGHT) {
        Serial.printf("[EPD] ERROR: panel is %ux%u but the build expects %dx%d\n", _eink->getDisplayWidth(),
                      _eink->getDisplayHeight(), EPD_WIDTH, EPD_HEIGHT);
    }

    // 3. Set display rotation (defaults to portrait via config.h)
    setRotation(DISPLAY_ROTATION);

    clear(EPD_WHITE);

    Serial.printf("[EPD] FreeInk Display initialized successfully (Controller: %s, Rotation: %d [%dx%d]).\n",
                  getControllerName(), rotation, width(), height());
    return true;
}

void EPDDisplay::reset() {
    // Handled internally by FreeInk
}

void EPDDisplay::sleep() {
    if (_eink) {
        _eink->deepSleep();
    }
}

void EPDDisplay::wake() {
    if (_eink) {
        _eink->begin();
    }
}

uint8_t* EPDDisplay::getBuffer() {
    return _eink ? _eink->getFrameBuffer() : nullptr;
}

const char* EPDDisplay::getControllerName() const {
    return _eink ? _eink->getControllerName() : "Unknown";
}

void EPDDisplay::clear(EPDColor color) {
    if (_eink) {
        _eink->clearScreen((color == EPD_WHITE) ? 0xFF : 0x00);
    }
}

void EPDDisplay::update(RefreshMode mode) {
    if (!_eink) return;
    uint32_t t0 = millis();
    if (mode == REFRESH_FULL) {
        Serial.println("[EPD] Updating display (FULL, blocking)...");
        _eink->displayBuffer(freeink::FreeInkDisplay::FULL_REFRESH);
    } else {
#if EPD_ASYNC_FAST_REFRESH
        // Non-blocking: returns while the panel refreshes, but in single-buffer mode
        // the SDK keeps a second 48 KB frame (the async baseline) allocated for good.
        Serial.println("[EPD] Updating display (FAST_REFRESH, async)...");
        _eink->displayBufferAsync(freeink::FreeInkDisplay::FAST_REFRESH);
#else
        // Blocking: no extra frame buffer; the page appears at the same moment.
        Serial.println("[EPD] Updating display (FAST_REFRESH)...");
        _eink->displayBuffer(freeink::FreeInkDisplay::FAST_REFRESH);
#endif
    }
    Serial.printf("[EPD] Display update dispatched/completed in %lu ms.\n", millis() - t0);
}

void EPDDisplay::waitSync() {
    if (_eink && _eink->isRefreshPending()) {
        _eink->finishDisplayAsync();
    }
}

void EPDDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (!_eink) return;
    uint8_t* buffer = _eink->getFrameBuffer();
    if (!buffer) return;

    // Handle Adafruit_GFX rotation
    int16_t t;
    switch (rotation) {
    case 1:
        t = x;
        x = EPD_WIDTH - 1 - y;
        y = t;
        break;
    case 2:
        x = EPD_WIDTH - 1 - x;
        y = EPD_HEIGHT - 1 - y;
        break;
    case 3:
        t = x;
        x = y;
        y = EPD_HEIGHT - 1 - t;
        break;
    default:
        break;
    }

    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;

    int byteIdx = (x / 8) + (y * (EPD_WIDTH / 8));
    uint8_t bitMask = 0x80 >> (x % 8);

    if (color == EPD_WHITE || color != 0) {
        buffer[byteIdx] |= bitMask;
    } else {
        buffer[byteIdx] &= ~bitMask;
    }
}

void EPDDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (y < 0 || y >= _height || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > _width) w = _width - x;
    if (w <= 0) return;

    for (int16_t i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
    }
}

void EPDDisplay::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    if (x < 0 || x >= _width || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > _height) h = _height - y;
    if (h <= 0) return;

    for (int16_t j = 0; j < h; j++) {
        drawPixel(x, y + j, color);
    }
}

void EPDDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void EPDDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void EPDDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = 0; i < h; i++) {
        drawFastHLine(x, y + i, w, color);
    }
}

void EPDDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color, uint16_t bg) {
    int byteWidth = (w + 7) / 8;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (bitmap[j * byteWidth + i / 8] & (128 >> (i & 7))) {
                drawPixel(x + i, y + j, color);
            } else if (bg != color) {
                drawPixel(x + i, y + j, bg);
            }
        }
    }
}
