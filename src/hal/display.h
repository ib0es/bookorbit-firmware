#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <XteinkDetect.h>
#include <EInkDisplay.h>

// Native (landscape) panel size. X3 (UC8253/UC8279, 3.7") shares the X4 pinout
// but has a 792x528 panel; each device is its own build env, so this is fixed
// at compile time and must match FreeInkDisplay's geometry after begin().
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !(defined(FREEINK_DEVICE_X4) && FREEINK_DEVICE_X4)
#define EPD_WIDTH            792
#define EPD_HEIGHT           528
#else
#define EPD_WIDTH            800
#define EPD_HEIGHT           480
#endif
#define EPD_BUFFER_SIZE      ((EPD_WIDTH * EPD_HEIGHT) / 8) // 48,000 bytes (X4), 52,272 (X3)

enum EPDColor {
    EPD_BLACK = 0,
    EPD_WHITE = 1
};

enum RefreshMode {
    REFRESH_FULL,    // Full refresh with waveform flashing (clears ghosting)
    REFRESH_PARTIAL  // Quick differential refresh
};

class EPDDisplay : public Adafruit_GFX {
public:
    EPDDisplay();
    virtual ~EPDDisplay();

    // Hardware lifecycle
    // Xteink X4/X3: pick the board profile and the panel controller of this
    // unit's batch. The controller probe bit-bangs the display pins, which the
    // SD card shares on these boards, so FreeInk wants it before
    // SDCardManager::begin() and FreeInkDisplay::begin() (SDK README, "X3 and
    // X4 share the ESP32-C3..."). begin() calls it if it has not run yet.
    void detectBoard();
    bool begin();
    void reset();
    void sleep();
    void wake();

    // Buffer management
    void clear(EPDColor color = EPD_WHITE);
    void update(RefreshMode mode = REFRESH_FULL);
    void waitSync();

    // Drawing primitives (Adafruit_GFX overrides)
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override;
    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[], int16_t w, int16_t h, uint16_t color, uint16_t bg = EPD_WHITE);

    // Framebuffer access
    uint8_t* getBuffer();
    int rawWidth() const { return EPD_WIDTH; }
    int rawHeight() const { return EPD_HEIGHT; }
    const char* getControllerName() const;

private:
    freeink::FreeInkDisplay* _eink;
    bool _boardDetected = false;
};

extern EPDDisplay Display;
