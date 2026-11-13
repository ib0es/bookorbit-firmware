#pragma once
// Test stub (test/sim): Wi-Fi radio for the real BookOrbitClient. Superset of
// simulator/include/WiFi.h. FakeNet::wifiUp decides whether "connecting" works.
#include "Arduino.h"

enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
enum wifi_auth_mode_t { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 };
enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

namespace FakeNet {
bool wifiBegin(uint32_t& connectMs, uint8_t& failReason);
}   // namespace FakeNet

// Station events, as Arduino-ESP32 has them (only the disconnect is used).
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5, ARDUINO_EVENT_MAX = 100 };
struct arduino_event_info_t {
    struct {
        uint8_t reason;
    } wifi_sta_disconnected;
};
typedef void (*WiFiEventSimCb)(arduino_event_id_t, arduino_event_info_t);

class IPAddress {
public:
    IPAddress() : _v(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _v((uint32_t)a << 24 | b << 16 | c << 8 | d) {}
    bool operator!=(const IPAddress& o) const { return _v != o._v; }
    bool operator==(const IPAddress& o) const { return _v == o._v; }
    String toString() const {
        char b[20];
        snprintf(b, sizeof(b), "%u.%u.%u.%u", _v >> 24, (_v >> 16) & 255, (_v >> 8) & 255, _v & 255);
        return String(b);
    }
    uint32_t _v;
};

class WiFiClass {
public:
    void mode(wifi_mode_t m) {
        _mode = m;
        if (m == WIFI_OFF) _connected = false;
    }
    wifi_mode_t getMode() { return _mode; }
    void setSleep(bool) {}
    void begin(const char* ssid, const char*) {
        _ssid = ssid ? ssid : "";
        uint32_t connectMs = 0;
        uint8_t failReason = 0;
        _connected = FakeNet::wifiBegin(connectMs, failReason);
        _connectedAt = millis() + connectMs;
        if (!_connected && failReason && _onDisconnect) {   // the driver reports why, twice (a retry)
            arduino_event_info_t info{};
            info.wifi_sta_disconnected.reason = failReason;
            _onDisconnect(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
            _onDisconnect(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
        }
    }
    int onEvent(WiFiEventSimCb cb, arduino_event_id_t event = ARDUINO_EVENT_MAX) {
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) _onDisconnect = cb;
        return 1;
    }
    void disconnect(bool = false, bool = false) { _connected = false; }
    int status() { return _connected && (int32_t)(millis() - _connectedAt) >= 0 ? WL_CONNECTED : WL_DISCONNECTED; }
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
    uint32_t _connectedAt = 0;
    WiFiEventSimCb _onDisconnect = nullptr;
    bool _connected = false;
    std::string _ssid;
};

inline WiFiClass WiFi;
