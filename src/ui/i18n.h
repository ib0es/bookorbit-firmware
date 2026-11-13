#pragma once

#include <Arduino.h>
#include "i18n_generated.h"

class I18n {
public:
    static void setLanguage(AppLanguage lang);
    static AppLanguage getLanguage();
    static const char* get(MsgId id);
    static const char* getLanguageName(AppLanguage lang);
    static const char* getLanguageCode(AppLanguage lang);
    static AppLanguage languageFromCode(const String& code);
    static int getLanguageCount();
};

inline const char* tr(MsgId id) { return I18n::get(id); }
