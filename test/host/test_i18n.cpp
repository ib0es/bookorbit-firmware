#include <cassert>
#include <cstdio>
#include <cstring>
#include "ui/i18n.h"

int main() {
    printf("Testing I18n subsystem...\n");

    // 1. Language count
    assert(I18n::getLanguageCount() == 2);

    // 2. Default language should be EN
    I18n::setLanguage(AppLanguage::EN);
    assert(I18n::getLanguage() == AppLanguage::EN);
    assert(strcmp(I18n::getLanguageCode(AppLanguage::EN), "en") == 0);
    assert(strcmp(I18n::getLanguageName(AppLanguage::EN), "English") == 0);

    // 3. String lookup in EN
    const char* enHome = tr(MsgId::HOME_APP_TITLE);
    assert(strcmp(enHome, "book orbit") == 0);
    const char* enSettings = tr(MsgId::HOME_CAT_SETTINGS);
    assert(strcmp(enSettings, "Settings") == 0);
    const char* enLang = tr(MsgId::SETTINGS_ITEM_LANGUAGE);
    assert(strcmp(enLang, "Interface Language") == 0);

    // 4. Switch to RU
    I18n::setLanguage(AppLanguage::RU);
    assert(I18n::getLanguage() == AppLanguage::RU);
    assert(strcmp(I18n::getLanguageCode(AppLanguage::RU), "ru") == 0);
    assert(strcmp(I18n::getLanguageName(AppLanguage::RU), "Русский") == 0);

    const char* ruHome = tr(MsgId::HOME_APP_TITLE);
    assert(strcmp(ruHome, "book orbit") == 0);
    const char* ruSettings = tr(MsgId::HOME_CAT_SETTINGS);
    assert(strcmp(ruSettings, "Настройки") == 0);
    const char* ruLang = tr(MsgId::SETTINGS_ITEM_LANGUAGE);
    assert(strcmp(ruLang, "Язык интерфейса") == 0);

    // 5. Switch by code
    AppLanguage langEn = I18n::languageFromCode("en");
    assert(langEn == AppLanguage::EN);
    I18n::setLanguage(langEn);
    assert(I18n::getLanguage() == AppLanguage::EN);
    assert(strcmp(tr(MsgId::HOME_CAT_SETTINGS), "Settings") == 0);

    AppLanguage langRu = I18n::languageFromCode("ru");
    assert(langRu == AppLanguage::RU);
    I18n::setLanguage(langRu);
    assert(I18n::getLanguage() == AppLanguage::RU);
    assert(strcmp(tr(MsgId::HOME_CAT_SETTINGS), "Настройки") == 0);

    // Unknown code should fallback to EN
    assert(I18n::languageFromCode("de") == AppLanguage::EN);
    assert(I18n::languageFromCode("unknown") == AppLanguage::EN);

    // 6. Test indices
    assert(strcmp(I18n::getLanguageCode(static_cast<AppLanguage>(0)), "en") == 0);
    assert(strcmp(I18n::getLanguageName(static_cast<AppLanguage>(0)), "English") == 0);
    assert(strcmp(I18n::getLanguageCode(static_cast<AppLanguage>(1)), "ru") == 0);
    assert(strcmp(I18n::getLanguageName(static_cast<AppLanguage>(1)), "Русский") == 0);

    printf("All I18n tests passed successfully!\n");
    return 0;
}
