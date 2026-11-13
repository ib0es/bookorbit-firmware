#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>
#include "Print.h"

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(PSTR(string_literal)))
#define PSTR(s) (s)

inline float radians(float deg) { return deg * 0.017453292519943295769236907684886f; }
inline float degrees(float rad) { return rad * 57.295779513082320876798154814105f; }

// Like arduino-esp32 3.x: in C++ min/max are std::min/std::max, not macros
// (FreeInkUI declares functions named min/max, which macros would break).
#ifdef __cplusplus
#include <algorithm>
using std::min;
using std::max;
#else
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

inline uint32_t millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}

inline uint32_t micros() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
}

void simPumpEvents();

inline void delay(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        simPumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

inline void delayMicroseconds(uint32_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }

inline void vTaskDelay(int) { simPumpEvents(); }

inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline int analogRead(int) { return 0; }

// Arduino String Class Emulation
class String {
public:
    String() : _s() {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(char c) : _s(1, c) {}
    String(int val) : _s(std::to_string(val)) {}
    String(unsigned int val) : _s(std::to_string(val)) {}
    String(long val) : _s(std::to_string(val)) {}
    String(unsigned long val) : _s(std::to_string(val)) {}
    String(float val, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, val);
        _s = buf;
    }

    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }
    void reserve(size_t sz) { _s.reserve(sz); }

    char charAt(size_t index) const { return index < _s.length() ? _s[index] : '\0'; }
    char operator[](size_t index) const { return charAt(index); }
    char& operator[](size_t index) { return _s[index]; }

    int indexOf(char c, size_t fromIndex = 0) const {
        size_t pos = _s.find(c, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int indexOf(const String& s, size_t fromIndex = 0) const {
        size_t pos = _s.find(s._s, fromIndex);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int lastIndexOf(char c) const {
        size_t pos = _s.rfind(c);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }
    int lastIndexOf(const String& s) const {
        size_t pos = _s.rfind(s._s);
        return (pos == std::string::npos) ? -1 : (int)pos;
    }

    String substring(size_t fromIndex, size_t toIndex = std::string::npos) const {
        if (fromIndex >= _s.length()) return String("");
        if (toIndex == std::string::npos || toIndex > _s.length()) {
            return String(_s.substr(fromIndex));
        }
        return String(_s.substr(fromIndex, toIndex - fromIndex));
    }

    bool startsWith(const String& prefix) const { return _s.rfind(prefix._s, 0) == 0; }
    bool endsWith(const String& suffix) const {
        if (_s.length() < suffix._s.length()) return false;
        return _s.compare(_s.length() - suffix._s.length(), suffix._s.length(), suffix._s) == 0;
    }

    bool equals(const String& other) const { return _s == other._s; }
    bool equalsIgnoreCase(const String& other) const {
        if (_s.length() != other._s.length()) return false;
        for (size_t i = 0; i < _s.length(); i++) {
            if (tolower((unsigned char)_s[i]) != tolower((unsigned char)other._s[i])) return false;
        }
        return true;
    }

    void replace(const String& find, const String& replace) {
        size_t pos = 0;
        while ((pos = _s.find(find._s, pos)) != std::string::npos) {
            _s.replace(pos, find._s.length(), replace._s);
            pos += replace._s.length();
        }
    }

    int toInt() const {
        try {
            return std::stoi(_s);
        } catch (...) {
            return 0;
        }
    }
    float toFloat() const {
        try {
            return std::stof(_s);
        } catch (...) {
            return 0.0f;
        }
    }

    String& operator+=(const String& rhs) {
        _s += rhs._s;
        return *this;
    }
    String& operator+=(const char* rhs) {
        if (rhs) _s += rhs;
        return *this;
    }
    String& operator+=(char c) {
        _s += c;
        return *this;
    }
    String& operator+=(int val) {
        _s += std::to_string(val);
        return *this;
    }

    bool operator==(const String& rhs) const { return _s == rhs._s; }
    bool operator==(const char* rhs) const { return rhs && _s == rhs; }
    bool operator!=(const String& rhs) const { return _s != rhs._s; }
    bool operator!=(const char* rhs) const { return !rhs || _s != rhs; }
    bool operator<(const String& rhs) const { return _s < rhs._s; }

    friend String operator+(const String& lhs, const String& rhs) { return String(lhs._s + rhs._s); }
    friend String operator+(const String& lhs, const char* rhs) { return String(lhs._s + (rhs ? rhs : "")); }
    friend String operator+(const char* lhs, const String& rhs) { return String((lhs ? lhs : "") + rhs._s); }

    void toLowerCase() {
        for (char& c : _s) c = tolower((unsigned char)c);
    }
    void toUpperCase() {
        for (char& c : _s) c = toupper((unsigned char)c);
    }
    int compareTo(const String& other) const { return _s.compare(other._s); }
    bool concat(const char* s) {
        if (s) _s += s;
        return true;
    }
    bool concat(const String& s) {
        _s += s._s;
        return true;
    }

    void remove(size_t index, size_t count = std::string::npos) {
        if (index < _s.length()) {
            if (count == std::string::npos)
                _s.erase(index);
            else
                _s.erase(index, count);
        }
    }

    void trim() {
        while (!_s.empty() && isspace((unsigned char)_s.front())) _s.erase(_s.begin());
        while (!_s.empty() && isspace((unsigned char)_s.back())) _s.pop_back();
    }

    const std::string& str() const { return _s; }

private:
    std::string _s;
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() {}
    virtual size_t readBytes(char* buffer, size_t length) {
        size_t count = 0;
        while (count < length) {
            int c = read();
            if (c < 0) break;
            *buffer++ = (char)c;
            count++;
        }
        return count;
    }
};

typedef int gpio_num_t;
inline void gpio_hold_dis(gpio_num_t) {}
inline void gpio_deep_sleep_hold_dis() {}

// Serial Emulation
class HardwareSerial : public Stream {
public:
    void begin(unsigned long) {}
    void end() {}
    void setTxTimeoutMs(uint32_t) {}
    size_t write(uint8_t b) override {
        putchar(b);
        return 1;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void print(const char* s) {
        if (s) {
            fputs(s, stdout);
            fflush(stdout);
        }
    }
    void print(int val) {
        printf("%d", val);
        fflush(stdout);
    }
    void println(const char* s = "") {
        if (s)
            puts(s);
        else
            putchar('\n');
        fflush(stdout);
    }
    void println(int val) {
        printf("%d\n", val);
        fflush(stdout);
    }
    void println(const String& s) {
        puts(s.c_str());
        fflush(stdout);
    }
    template <typename... Args>
    void printf(const char* fmt, Args... args) {
        ::printf(fmt, args...);
        fflush(stdout);
    }
};

inline HardwareSerial Serial;

// Free heap reported by the simulator; tests lower it to exercise memory guards.
inline uint32_t g_simFreeHeap = 184 * 1024;

class EspClass {
public:
    uint32_t getFreeHeap() { return g_simFreeHeap; }
};
inline EspClass ESP;

#define RTC_DATA_ATTR
