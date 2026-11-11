#pragma once

// Frontlight (X4 Pro: warm/cool PWM; Picco: 2-channel I2C driver) over the SDK's
// FrontlightManager. On boards without one every call is a no-op and present()
// is false, so the settings UI hides the item.

#include <Arduino.h>

class FrontlightHelper {
public:
    bool present() const;
    bool hasWarmth() const;   // warm/cool mix available
    void begin();
    // brightness 0..100 (0 = off), warm 0..100 (0 = cool, 100 = warm)
    void apply(int brightness, int warm);
    int brightness() const { return _brightness; }
    int warmth() const { return _warm; }
    // Parks the LEDs off for deep sleep (call before the rails go down).
    void prepareForSleep();
    // Hardware light switch: when off, the LEDs stay dark but the configured
    // brightness is kept and comes back when the switch is turned on again.
    void setEnabled(bool on);
    bool enabled() const { return _enabled; }

private:
    int _brightness = 0;
    int _warm = 50;
    bool _enabled = true;
    void push();
};

extern FrontlightHelper Frontlight;
