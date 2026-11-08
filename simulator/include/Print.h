#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class __FlashStringHelper;
class Print;

class Printable {
public:
    virtual ~Printable() = default;
    virtual size_t printTo(Print& p) const = 0;
};

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            if (write(*buffer++)) n++;
            else break;
        }
        return n;
    }

    size_t print(const char* s) {
        if (!s) return 0;
        size_t n = 0;
        while (*s) {
            write((uint8_t)*s++);
            n++;
        }
        return n;
    }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", n);
        return print(buf);
    }
    size_t println(const char* s = "") {
        size_t n = print(s);
        n += write('\n');
        return n;
    }
};
