#pragma once
// Test stub (test/sim): Wi-Fi radio for the real BookOrbitClient. Superset of
// simulator/include/WiFi.h. FakeNet::wifiUp decides whether "connecting" works.
#include "Arduino.h"

enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

namespace FakeNet { bool wifiBegin(); }   // test/sim/fake_bookorbit.cpp

class IPAddress {
public:
    IPAddress() : _v(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _v((uint32_t)a << 24 | b << 16 | c << 8 | d) {}
    bool operator!=(const IPAddress& o) const { return _v != o._v; }
    bool operator==(const IPAddress& o) const { return _v == o._v; }
    String toString() const {
        char b[20]; snprintf(b, sizeof(b), "%u.%u.%u.%u", _v >> 24, (_v >> 16) & 255, (_v >> 8) & 255, _v & 255);
        return String(b);
    }
    uint32_t _v;
};

class WiFiClass {
public:
    void mode(wifi_mode_t m) { _mode = m; if (m == WIFI_OFF) _connected = false; }
    wifi_mode_t getMode() { return _mode; }
    void setSleep(bool) {}
    void begin(const char* ssid, const char*) { _ssid = ssid ? ssid : ""; _connected = FakeNet::wifiBegin(); }
    void disconnect(bool = false, bool = false) { _connected = false; }
    int status() { return _connected ? WL_CONNECTED : WL_DISCONNECTED; }
    String SSID() { return String(_ssid.c_str()); }
    int RSSI() { return -55; }
    IPAddress localIP() { return _connected ? IPAddress(192, 168, 1, 150) : IPAddress(); }
    int scanNetworks(bool = false, bool = false) { return 2; }
    String SSID(int i) { return (i == 0) ? "HomeNetwork" : "BookOrbit_WiFi"; }
    int RSSI(int i) { return (i == 0) ? -48 : -68; }
    wifi_auth_mode_t encryptionType(int i) { return (i == 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN; }
    void scanDelete() {}
    bool softAP(const char*, const char* = nullptr) { return true; }
    IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
    bool softAPdisconnect(bool = false) { return true; }
private:
    wifi_mode_t _mode = WIFI_OFF;
    bool _connected = false;
    std::string _ssid;
};

inline WiFiClass WiFi;
