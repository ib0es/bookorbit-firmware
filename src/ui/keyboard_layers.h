#pragma once

#include "i18n.h"

// Letter keyboards of a text field. Latin and the interface language's own keyboard are always there
// (built in); the user can add others. Symbols are always on, Latin is forced for passwords and addresses.
namespace KeyboardLayers {

enum : int { LATIN = 1, CYRILLIC = 2, ACCENTS = 4, ALL = 7 };

inline int builtIn(AppLanguage language) {
    switch (language) {
        case AppLanguage::RU:
            return LATIN | CYRILLIC;
        case AppLanguage::ES:
        case AppLanguage::DE:
        case AppLanguage::FR:
        case AppLanguage::IT:
        case AppLanguage::PT_PT:
        case AppLanguage::PT_BR:
            return LATIN | ACCENTS;
        default:   // English, and the languages that have no keyboard of their own yet
            return LATIN;
    }
}

inline int effective(int extras, AppLanguage language) { return builtIn(language) | (extras & ALL); }

// The keyboards the user may add: those the language does not already bring.
inline int optionCount(AppLanguage language) {
    int count = 0;
    for (int bit = LATIN; bit <= ACCENTS; bit <<= 1) count += (builtIn(language) & bit) ? 0 : 1;
    return count;
}

inline int optionBit(AppLanguage language, int index) {
    for (int bit = LATIN; bit <= ACCENTS; bit <<= 1)
        if (!(builtIn(language) & bit) && index-- == 0) return bit;
    return 0;
}

}  // namespace KeyboardLayers
