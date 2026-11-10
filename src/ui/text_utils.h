#pragma once

#include <Arduino.h>

// The first bytes of a UTF-8 string, at most `maxBytes`, never cut inside a
// character (String::substring counts bytes: "Дракон Возрожденный" cut at 32
// left half a letter on the screen). The screens shorten what does not fit
// with an ellipsis themselves; this only bounds what is passed to them.
inline String utf8Prefix(const String& s, size_t maxBytes) {
    if (s.length() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;   // s[cut] starts the next character
    return s.substring(0, cut);
}
