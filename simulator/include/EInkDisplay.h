#pragma once

#include <cstdint>
#include <cstring>

namespace freeink {

typedef void (*DisplayUpdateCallback)(const uint8_t* buffer, int mode);

class FreeInkDisplay {
public:
    enum RefreshMode { FULL_REFRESH = 0, FAST_REFRESH = 1 };

    FreeInkDisplay(int, int, int, int, int, int) {
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
        _w = 792;
        _h = 528;
#elif defined(FREEINK_DEVICE_EEGO_A4) && FREEINK_DEVICE_EEGO_A4
        _w = 768;
        _h = 552;
#elif (defined(FREEINK_DEVICE_LILYGO) && FREEINK_DEVICE_LILYGO) || \
      (defined(FREEINK_DEVICE_PAPERS3) && FREEINK_DEVICE_PAPERS3)
        _w = 960;
        _h = 540;
#else
        _w = 800;
        _h = 480;
#endif
        memset(_fb, 0xFF, sizeof(_fb));
    }
    ~FreeInkDisplay() = default;

    void begin() {}

    // Geometry: X4 800x480 by default, X3 792x528 after setDisplayX3().
    void setDisplayX3() {
        _w = 792;
        _h = 528;
    }
    uint16_t getDisplayWidth() const { return _w; }
    uint16_t getDisplayHeight() const { return _h; }
    uint16_t getDisplayWidthBytes() const { return _w / 8; }
    uint32_t getBufferSize() const { return (uint32_t)_w * _h / 8; }
    void deepSleep() {}
    uint8_t* getFrameBuffer() { return _buildLent ? nullptr : _fb; }
    uint8_t* lendBuildStorage(uint32_t* size) {
        if (size) *size = _buildLent ? 0 : getBufferSize();
        if (_buildLent) return nullptr;
        _buildLent = true;
        return _fb;
    }
    void returnBuildStorage() {
        if (!_buildLent) return;
        _buildLent = false;
        memset(_fb, 0xFF, getBufferSize());
    }

    void clearScreen(uint8_t colorByte = 0xFF) { memset(_fb, colorByte, getBufferSize()); }

    const char* getControllerName() const { return _w == 792 ? "UC8253 (Simulated)" : "SSD1677 (Simulated)"; }

    void displayBuffer(RefreshMode mode) {
        if (s_callback) s_callback(_fb, (int)mode);
    }

    void displayBufferAsync(RefreshMode mode) {
        if (s_callback) s_callback(_fb, (int)mode);
    }

    bool isRefreshPending() { return false; }
    bool refreshBusy() { return false; }
    void finishDisplayAsync() {}
    void setBusyWaitSliceHook(bool (*)(int8_t, uint8_t)) {}   // no busy waits here

    static void setUpdateCallback(DisplayUpdateCallback cb) { s_callback = cb; }
    static DisplayUpdateCallback s_callback;

private:
    bool _buildLent = false;
    uint16_t _w = 800, _h = 480;
    uint8_t _fb[65536]; // large enough for all simulated panels (960 * 540 / 8 = 64800)
};

} // namespace freeink

// Same alias as freeink-sdk/libs/display/FreeInkDisplay/include/EInkDisplay.h
using EInkDisplay = freeink::FreeInkDisplay;
