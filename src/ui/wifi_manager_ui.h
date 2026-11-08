#pragma once

#include <Arduino.h>
#include "keyboard_ui.h"

class WifiManagerUI {
public:
    WifiManagerUI();

    // Scans for available Wi-Fi networks, displays list,
    // lets user choose network and enter password (via on-screen keyboard or web),
    // and connects to it.
    // Returns true if successfully connected and updated outSsid/outPass.
    bool runScanAndConnect(String& outSsid, String& outPass);

private:
    bool showOnScreenKeyboard(const String& ssid, String& inOutPass);
};

extern WifiManagerUI WifiUI;
