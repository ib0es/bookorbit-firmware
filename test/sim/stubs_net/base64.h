#pragma once
// Test stub (test/sim): Arduino-ESP32 base64::encode.
#include "Arduino.h"
class base64 {
public:
    static String encode(const String& in) {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string s = in.c_str(), o;
        int val = 0, bits = -6;
        for (unsigned char c : s) {
            val = (val << 8) + c; bits += 8;
            while (bits >= 0) { o.push_back(t[(val >> bits) & 0x3F]); bits -= 6; }
        }
        if (bits > -6) o.push_back(t[((val << 8) >> (bits + 8)) & 0x3F]);
        while (o.size() % 4) o.push_back('=');
        return String(o.c_str());
    }
};
