#pragma once

#include <Arduino.h>

enum KeyboardMode { KB_MODE_TEXT = 0, KB_MODE_PASSWORD, KB_MODE_URL };

class KeyboardUI {
public:
    // Displays full on-screen keyboard for text/password/URL entry using physical buttons.
    // title: Header title (e.g. "ПАРОЛЬ WI-FI")
    // subtitle: Context hint (e.g. "Сеть: MyHome")
    // inOutText: String to edit; updated if confirmed (OK).
    // mode: KB_MODE_PASSWORD, KB_MODE_TEXT, or KB_MODE_URL
    // Returns true if user clicked [OK ✓], false if cancelled.
    static bool show(const String& title, const String& subtitle, String& inOutText,
                     KeyboardMode mode = KB_MODE_PASSWORD);
};
