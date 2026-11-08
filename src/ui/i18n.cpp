#include "i18n.h"

static AppLanguage s_currentLanguage = AppLanguage::EN;

void I18n::setLanguage(AppLanguage lang) {
    if ((size_t)lang < (size_t)AppLanguage::COUNT) {
        s_currentLanguage = lang;
    } else {
        s_currentLanguage = AppLanguage::EN;
    }
}

AppLanguage I18n::getLanguage() {
    return s_currentLanguage;
}

const char* I18n::get(MsgId id) {
    size_t idIdx = (size_t)id;
    if (idIdx >= (size_t)MsgId::COUNT) {
        return "";
    }

    size_t langIdx = (size_t)s_currentLanguage;
    if (langIdx >= (size_t)AppLanguage::COUNT) {
        langIdx = (size_t)AppLanguage::EN;
    }

    const char* str = I18N_ALL_TABLES[langIdx][idIdx];
    if (!str || !*str) {
        // Fallback to English
        str = STRINGS_EN[idIdx];
    }
    return str ? str : "";
}

const char* I18n::getLanguageName(AppLanguage lang) {
    size_t idx = (size_t)lang;
    if (idx < (size_t)AppLanguage::COUNT) {
        return I18N_LANG_NAMES[idx];
    }
    return "English";
}

const char* I18n::getLanguageCode(AppLanguage lang) {
    size_t idx = (size_t)lang;
    if (idx < (size_t)AppLanguage::COUNT) {
        return I18N_LANG_CODES[idx];
    }
    return "en";
}

AppLanguage I18n::languageFromCode(const String& code) {
    for (size_t i = 0; i < (size_t)AppLanguage::COUNT; i++) {
        if (code.equalsIgnoreCase(I18N_LANG_CODES[i])) {
            return (AppLanguage)i;
        }
    }
    return AppLanguage::EN;
}

int I18n::getLanguageCount() {
    return (int)AppLanguage::COUNT;
}
