#pragma once
#include "Arduino.h"

enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };

class IPAddress {
public:
    String toString() const { return "192.168.1.150"; }
};

class WiFiClass {
public:
    void mode(wifi_mode_t m) { _mode = m; }
    wifi_mode_t getMode() const { return _mode; }
    void disconnect() { _mode = WIFI_OFF; }
    int status() { return 3; }
    int RSSI() { return -55; }
    int scanNetworks(bool async = false, bool show_hidden = false) { return 2; }
    String SSID(int i) { return (i == 0) ? "HomeNetwork" : "BookOrbit_WiFi"; }
    int RSSI(int i) { return (i == 0) ? -48 : -68; }
    wifi_auth_mode_t encryptionType(int i) { return (i == 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN; }
    void scanDelete() {}
    IPAddress localIP() { return IPAddress(); }

private:
    wifi_mode_t _mode = WIFI_OFF;
};

inline WiFiClass WiFi;
