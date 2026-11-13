#pragma once

enum esp_sleep_wakeup_cause_t {
    ESP_SLEEP_WAKEUP_UNDEFINED = 0,
    ESP_SLEEP_WAKEUP_EXT0,
    ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER,
    ESP_SLEEP_WAKEUP_GPIO
};

// What the next setup() sees as the reset reason (the headless 'Z' step sets
// a wake-up from deep sleep).
inline esp_sleep_wakeup_cause_t g_simWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;

inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return g_simWakeCause; }
