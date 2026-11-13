// The ESP32 side of platform/system.h (the simulator's: simulator/src/platform_sim.cpp).
#include "../system.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include "config.h"

namespace Platform {

time_t systemTime() { return time(nullptr); }

void setSystemTime(time_t utc) {
    struct timeval tv = {utc, 0};
    settimeofday(&tv, nullptr);
}

bool restartSupported() { return true; }

void restart() {
    Serial.flush();
    esp_restart();
    while (true) {}
}

uint32_t sleepTimeoutMs() { return SLEEP_TIMEOUT_MS; }
uint32_t batteryCheckIntervalMs() { return 60000; }

bool usbSerialConnected() {
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    return Serial.isConnected();
#else
    return false;
#endif
}

void feedWatchdog() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) {
        esp_task_wdt_reset();
    }
}

namespace {
struct WakeKey {
    int8_t pin;
    int activeLevel;
};
// Every key of the board, at its pressed level.
void wakeKeys(WakeKey (&out)[7]) {
    const auto& bi = BoardConfig::ACTIVE.input;
    const WakeKey keys[7] = {
            {bi.up, 0},
            {bi.down, 0},
            {bi.left, 0},
            {bi.right, 0},
            {bi.confirm, 0},
            {bi.back, 0},
            {bi.power, bi.powerActiveHigh ? 1 : 0},
    };
    for (int i = 0; i < 7; i++) out[i] = keys[i];
}
}  // namespace

bool wakeInputActive(int8_t irqPin, bool irqActiveLow) {
    bool active = false;
    const int irqActiveLevel = irqActiveLow ? 0 : 1;
    if (irqPin >= 0 && gpio_get_level(static_cast<gpio_num_t>(irqPin)) == irqActiveLevel) active = true;
    WakeKey keys[7];
    wakeKeys(keys);
    for (const auto& wb : keys) {
        if (wb.pin >= 0 && gpio_get_level(static_cast<gpio_num_t>(wb.pin)) == wb.activeLevel) active = true;
    }
    return active;
}

void lightSleep(uint32_t waitMs, int8_t irqPin, bool irqActiveLow) {
    if (waitMs < 50) {
        delay(10);
        return;
    }
    WakeKey keys[7];
    wakeKeys(keys);
    gpio_wakeup_enable(static_cast<gpio_num_t>(irqPin), irqActiveLow ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    for (const auto& wb : keys) {
        if (wb.pin >= 0 && wb.pin != irqPin) {
            gpio_wakeup_enable(static_cast<gpio_num_t>(wb.pin),
                               wb.activeLevel ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
        }
    }
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(waitMs) * 1000ULL);
    esp_light_sleep_start();
    for (const auto& wb : keys) {
        if (wb.pin >= 0 && wb.pin != irqPin) gpio_wakeup_disable(static_cast<gpio_num_t>(wb.pin));
    }
    gpio_wakeup_disable(static_cast<gpio_num_t>(irqPin));
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
}

}  // namespace Platform
