#pragma once

#include <cstdint>
#include <cstring>

namespace freeink {

typedef void (*DisplayUpdateCallback)(const uint8_t* buffer, int mode);

class FreeInkDisplay {
public:
    enum RefreshMode {
        FULL_REFRESH = 0,
        FAST_REFRESH = 1
    };

    FreeInkDisplay(int, int, int, int, int, int) {
        memset(_fb, 0xFF, sizeof(_fb));
    }
    ~FreeInkDisplay() = default;

    void begin() {}

    // Geometry: X4 800x480 by default, X3 792x528 after setDisplayX3().
    void setDisplayX3() { _w = 792; _h = 528; }
    uint16_t getDisplayWidth() const { return _w; }
    uint16_t getDisplayHeight() const { return _h; }
    uint16_t getDisplayWidthBytes() const { return _w / 8; }
    uint32_t getBufferSize() const { return (uint32_t)_w * _h / 8; }
    void deepSleep() {}
    uint8_t* getFrameBuffer() { return _fb; }
    
    void clearScreen(uint8_t colorByte = 0xFF) {
        memset(_fb, colorByte, getBufferSize());
    }

    const char* getControllerName() const { return "SSD1677 (Simulated)"; }

    void displayBuffer(RefreshMode mode) {
        if (s_callback) s_callback(_fb, (int)mode);
    }

    void displayBufferAsync(RefreshMode mode) {
        if (s_callback) s_callback(_fb, (int)mode);
    }

    bool isRefreshPending() { return false; }
    bool refreshBusy() { return false; }
    void finishDisplayAsync() {}

    static void setUpdateCallback(DisplayUpdateCallback cb) { s_callback = cb; }
    static DisplayUpdateCallback s_callback;

private:
    uint16_t _w = 800, _h = 480;
    uint8_t _fb[52272]; // large enough for X3 (792 * 528 / 8); X4 uses 48000
};

} // namespace freeink

// Same alias as freeink-sdk/libs/display/FreeInkDisplay/include/EInkDisplay.h
using EInkDisplay = freeink::FreeInkDisplay;
