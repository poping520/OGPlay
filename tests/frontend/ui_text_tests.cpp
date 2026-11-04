#include <doctest/doctest.h>
#include <algorithm>
#include <stdexcept>
#include "ogplay/app/ui_text.h"

using namespace ogplay::app;

TEST_CASE("ui_text has complete bilingual entries") {
    for (std::uint16_t i = 0; i < static_cast<std::uint16_t>(UiText::count); ++i) {
        const auto id = static_cast<UiText>(i);
        CHECK_FALSE(Text(UiLanguage::zh_CN, id).empty());
        const auto english = Text(UiLanguage::en_US, id);
        CHECK_FALSE(english.empty());
        CHECK(std::all_of(english.begin(), english.end(), [](unsigned char c) { return c < 128; }));
    }
    CHECK_THROWS_AS(static_cast<void>(Text(UiLanguage::en_US, UiText::count)), std::out_of_range);
}

TEST_CASE("ui_text accepts only explicit supported language tags") {
    CHECK(ParseUiLanguage("zh-CN") == UiLanguage::zh_CN);
    CHECK(ParseUiLanguage("en-US") == UiLanguage::en_US);
    CHECK(UiLanguageTag(UiLanguage::zh_CN) == "zh-CN");
    CHECK(UiLanguageTag(UiLanguage::en_US) == "en-US");
    for (auto text : {"", "zh", "en", "zh_CN", "en-us", "system", "fr-FR"})
        CHECK_FALSE(ParseUiLanguage(text));
}

TEST_CASE("ui_text resolves Chinese locale variants and defaults other locales to English") {
    for (auto locale : {"zh", "zh-Hans-CN", "zh_TW", "zh-CN", "ZH-hant"})
        CHECK(UiLanguageForLocale(locale) == UiLanguage::zh_CN);
    for (auto locale : {"", "en", "en-US", "fr-FR", "zho", "zhgarbage"})
        CHECK(UiLanguageForLocale(locale) == UiLanguage::en_US);
    CHECK(ResolveUiLanguage("system") == SystemUiLanguage());
    CHECK(ResolveUiLanguage("zh-CN") == UiLanguage::zh_CN);
    CHECK(ResolveUiLanguage("en-US") == UiLanguage::en_US);
    for (auto text : {"", "zh", "en-us", "fr-FR"})
        CHECK_THROWS_AS(static_cast<void>(ResolveUiLanguage(text)), std::invalid_argument);
}
