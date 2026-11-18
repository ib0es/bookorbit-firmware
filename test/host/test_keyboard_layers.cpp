#include <cassert>
#include <cstdio>

#include "ui/keyboard_layers.h"

int main() {
    using namespace KeyboardLayers;
    assert(effective(0, AppLanguage::EN) == LATIN);
    assert(effective(0, AppLanguage::RU) == (LATIN | CYRILLIC));
    for (AppLanguage language :
         {AppLanguage::ES, AppLanguage::DE, AppLanguage::FR, AppLanguage::IT, AppLanguage::PT_PT, AppLanguage::PT_BR})
        assert(effective(0, language) == (LATIN | ACCENTS));
    for (AppLanguage language : {AppLanguage::ZH_HANS, AppLanguage::JA, AppLanguage::KO})
        assert(effective(0, language) == LATIN);
    // The language's own keyboards cannot be removed; others can be added.
    assert(effective(CYRILLIC, AppLanguage::EN) == (LATIN | CYRILLIC));
    assert(effective(ALL + 8, AppLanguage::EN) == ALL);
    assert(optionCount(AppLanguage::EN) == 2 && optionBit(AppLanguage::EN, 0) == CYRILLIC &&
           optionBit(AppLanguage::EN, 1) == ACCENTS);
    assert(optionCount(AppLanguage::RU) == 1 && optionBit(AppLanguage::RU, 0) == ACCENTS);
    assert(optionCount(AppLanguage::ES) == 1 && optionBit(AppLanguage::ES, 0) == CYRILLIC);
    assert(optionBit(AppLanguage::RU, 1) == 0);
    puts("OK: keyboard layers");
}
