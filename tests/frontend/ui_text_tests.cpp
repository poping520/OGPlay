#include <doctest/doctest.h>
#include <algorithm>
#include <ostream>
#include <stdexcept>
#include "ogplay/app/ui_text.h"

using namespace ogplay::app;

TEST_CASE("ui_text has complete entries in every language") {
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(UiText::count); ++i) {
        const auto id = static_cast<UiText>(i);
        for (const auto language : kUiLanguages)
            CHECK_FALSE(Text(language, id).empty());
        const auto english = Text(UiLanguage::en_US, id);
        CHECK(std::all_of(english.begin(), english.end(), [](unsigned char c) { return c < 128; }));
    }
    CHECK(Text(UiLanguage::ja_JP, UiText::close_game_title) == "ゲームを終了");
    CHECK(Text(UiLanguage::fr_FR, UiText::menu_quit) == "Quitter OGPlay");
    CHECK_THROWS_AS(static_cast<void>(Text(UiLanguage::en_US, UiText::count)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(Text(static_cast<UiLanguage>(255), UiText::menu_quit)), std::invalid_argument);
}

TEST_CASE("ui_text accepts only explicit supported language tags") {
    for (const auto language : kUiLanguages)
        CHECK(ParseUiLanguage(UiLanguageTag(language)) == language);
    CHECK(UiLanguageTag(UiLanguage::zh_CN) == "zh-CN");
    CHECK(UiLanguageTag(UiLanguage::en_US) == "en-US");
    CHECK(UiLanguageTag(UiLanguage::zh_TW) == "zh-TW");
    CHECK(UiLanguageTag(UiLanguage::pt_BR) == "pt-BR");
    for (auto text : {"", "zh", "en", "zh_CN", "en-us", "system", "ja", "pt-PT", "de-DE"})
        CHECK_FALSE(ParseUiLanguage(text));
    CHECK_THROWS_AS(static_cast<void>(UiLanguageTag(static_cast<UiLanguage>(255))), std::invalid_argument);
}

TEST_CASE("ui_text resolves system locales to the closest supported language") {
    for (auto locale : {"zh", "zh-Hans-CN", "zh-CN", "zh_SG", "ZH-hans"})
        CHECK(UiLanguageForLocale(locale) == UiLanguage::zh_CN);
    for (auto locale : {"zh_TW", "zh-HK", "zh-MO", "ZH-hant", "zh-Hant-CN"})
        CHECK(UiLanguageForLocale(locale) == UiLanguage::zh_TW);
    CHECK(UiLanguageForLocale("ja") == UiLanguage::ja_JP);
    CHECK(UiLanguageForLocale("ko-KR") == UiLanguage::ko_KR);
    CHECK(UiLanguageForLocale("ru_UA") == UiLanguage::ru_RU);
    CHECK(UiLanguageForLocale("es-MX") == UiLanguage::es_ES);
    CHECK(UiLanguageForLocale("pt-PT") == UiLanguage::pt_BR);
    CHECK(UiLanguageForLocale("FR-ca") == UiLanguage::fr_FR);
    for (auto locale : {"", "en", "en-US", "de-DE", "zho", "zhgarbage", "jav"})
        CHECK(UiLanguageForLocale(locale) == UiLanguage::en_US);
    CHECK(ResolveUiLanguage("system") == SystemUiLanguage());
    CHECK(ResolveUiLanguage("zh-CN") == UiLanguage::zh_CN);
    CHECK(ResolveUiLanguage("ko-KR") == UiLanguage::ko_KR);
    for (auto text : {"", "zh", "en-us", "de-DE"})
        CHECK_THROWS_AS(static_cast<void>(ResolveUiLanguage(text)), std::invalid_argument);
}
