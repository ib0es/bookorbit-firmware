#pragma once

// The few things the services need from the machine they run on, with one
// implementation for the ESP32 (src/platform/esp32/system.cpp) and one for the
// desktop simulator (simulator/src/platform_sim.cpp). The services never test
// ESP_PLATFORM themselves.

#include <cstdint>
#include <ctime>

namespace Platform {

// ---- the system clock (seconds since 1970; ~0 until it is set)
time_t systemTime();
void setSystemTime(time_t utc);

// ---- restart (the simulator has none: a restart there would end the run)
bool restartSupported();
[[noreturn]] void restart();   // only when restartSupported()

// ---- timings and the USB serial link (the simulator's tests change them)
uint32_t sleepTimeoutMs();          // idle time before deep sleep
uint32_t batteryCheckIntervalMs();  // how often the charge is looked at
bool usbSerialConnected();          // a host has the CDC serial port open
void feedWatchdog();                // feeds the task watchdog during long-running tasks

// ---- light sleep between loop ticks (touch-wake boards)
// A key or the touch IRQ is held down now: light sleep would wake at once.
bool wakeInputActive(int8_t touchIrqPin, bool irqActiveLow);
// Sleeps up to `ms`, woken by a key or the touch IRQ; a short wait is a plain
// delay. The simulator only waits 10 ms.
void lightSleep(uint32_t ms, int8_t touchIrqPin, bool irqActiveLow);

}  // namespace Platform
