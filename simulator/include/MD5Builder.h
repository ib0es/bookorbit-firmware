#pragma once

#include <Arduino.h>

#if defined(ESP32)
#include_next <MD5Builder.h>
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <CommonCrypto/CommonDigest.h>

class MD5Builder {
private:
    CC_MD5_CTX _ctx;
    uint8_t _buf[16];
    bool _calculated;

public:
    MD5Builder() : _calculated(false) {}

    void begin() {
        CC_MD5_Init(&_ctx);
        _calculated = false;
    }

    void add(const uint8_t* data, size_t len) {
        if (data && len > 0) {
            CC_MD5_Update(&_ctx, data, (CC_LONG)len);
        }
    }

    void add(const char* str) {
        if (str) {
            add((const uint8_t*)str, strlen(str));
        }
    }

    void add(const String& str) {
        add(str.c_str());
    }

    void calculate() {
        if (!_calculated) {
            CC_MD5_Final(_buf, &_ctx);
            _calculated = true;
        }
    }

    void getBytes(uint8_t* output) {
        calculate();
        if (output) {
            memcpy(output, _buf, 16);
        }
    }

    String toString() {
        calculate();
        char out[33];
        for (int i = 0; i < 16; i++) {
            snprintf(out + (i * 2), 3, "%02x", _buf[i]);
        }
        out[32] = '\0';
        return String(out);
    }
};
#pragma clang diagnostic pop
#endif
