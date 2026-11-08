#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <XteinkDetect.h>
#include <EInkDisplay.h>
#include "pins.h"

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
};

extern EPDDisplay Display;
