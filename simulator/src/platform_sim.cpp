// The desktop simulator's side of src/platform/system.h and font_partition.h
// (the ESP32's: src/platform/esp32/). Listed in simulator/Makefile only.
#include "platform/system.h"
#include "platform/font_partition.h"

#include <Arduino.h>
#include <cstdlib>

#include "config.h"
#include "platform/hal/storage.h"

namespace Platform {

namespace {
// The clock: the host's until it is set (from the RTC or the server), then
// an emulated one running from that moment.
time_t s_baseUtc = 0;
uint32_t s_setMs = 0;
}  // namespace

time_t systemTime() {
    if (s_baseUtc != 0) return (time_t)(s_baseUtc + (millis() - s_setMs) / 1000);
    return time(nullptr);
}

void setSystemTime(time_t utc) {
    s_baseUtc = utc;
    s_setMs = millis();
}

bool restartSupported() { return false; }
void restart() { std::abort(); }

uint32_t sleepTimeoutMs() {
    const char* v = std::getenv("SIM_SLEEP_TIMEOUT_MS");
    const long ms = (v && *v) ? std::atol(v) : 0;
    return ms > 0 ? static_cast<uint32_t>(ms) : SLEEP_TIMEOUT_MS;
}

uint32_t batteryCheckIntervalMs() {
    static const uint32_t kEveryMs = getenv("SIM_BATTERY_CHECK_MS") ? (uint32_t)atoi(getenv("SIM_BATTERY_CHECK_MS")) : 60000;
    return kEveryMs;
}

bool usbSerialConnected() {
    const char* v = std::getenv("SIM_USB_SERIAL");
    return v && *v == '1';
}

void feedWatchdog() {}

bool wakeInputActive(int8_t, bool) { return false; }
void lightSleep(uint32_t, int8_t, bool) { delay(10); }

}  // namespace Platform

namespace FontPartition {

namespace {
// The partition is a file on the card, read into memory when mapped.
constexpr const char* kFile = "/.fontpart.bin";
constexpr uint32_t kCapacity = 0x360000;
FsFile s_out;
uint8_t* s_base = nullptr;
}  // namespace

uint32_t capacity() { return kCapacity; }

bool read(uint32_t offset, void* dst, uint32_t len) {
    FsFile f = Storage.open(kFile, O_RDONLY);
    if (!f) return false;
    const bool ok = (offset == 0 || f.seek(offset)) && f.read(dst, len) == (int)len;
    f.close();
    return ok;
}

bool erase(uint32_t, void (*)(void*, int), void*) {
    if (s_out) s_out.close();
    s_out = Storage.open(kFile, O_RDWR | O_CREAT | O_TRUNC);
    if (!s_out) return false;
    static const uint8_t zero[256] = {0};
    for (uint32_t z = 0; z < 4096; z += sizeof(zero)) s_out.write(zero, sizeof(zero));
    return true;
}

bool write(uint32_t offset, const void* src, uint32_t len) {
    s_out.seek(offset);
    s_out.write((const uint8_t*)src, len);
    return true;
}

bool finish() {
    s_out.close();
    return true;
}

const uint8_t* map(uint32_t bytes) {
    FsFile f = Storage.open(kFile, O_RDONLY);
    if (!f) return nullptr;
    s_base = (uint8_t*)malloc(bytes);
    const bool ok = s_base && f.read(s_base, bytes) == (int)bytes;
    f.close();
    if (!ok) {
        free(s_base);
        s_base = nullptr;
    }
    return s_base;
}

void unmap() {
    free(s_base);
    s_base = nullptr;
}

}  // namespace FontPartition
