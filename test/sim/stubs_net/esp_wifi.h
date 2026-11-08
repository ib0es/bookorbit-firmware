#pragma once
// Test stub (test/sim): ESP-IDF Wi-Fi power save API.
typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1, WIFI_PS_MAX_MODEM = 2 } wifi_ps_type_t;
inline int esp_wifi_set_ps(wifi_ps_type_t) { return 0; }
