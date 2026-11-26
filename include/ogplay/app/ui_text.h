#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include "ogplay/app/export.h"

namespace ogplay::app {
// Launcher and guest languages; the tags (zh-CN, en-US, zh-TW, ja-JP, ko-KR, ru-RU, es-ES, pt-BR, fr-FR) are what
// settings and command lines carry.
enum class UiLanguage : std::uint8_t { zh_CN, en_US, zh_TW, ja_JP, ko_KR, ru_RU, es_ES, pt_BR, fr_FR };
inline constexpr std::array kUiLanguages{UiLanguage::zh_CN, UiLanguage::en_US, UiLanguage::zh_TW,
                                         UiLanguage::ja_JP, UiLanguage::ko_KR, UiLanguage::ru_RU,
                                         UiLanguage::es_ES, UiLanguage::pt_BR, UiLanguage::fr_FR};
enum class UiText : std::uint16_t {
    close_game_title,
    close_game_message,
    close_game_confirm,
    close_game_cancel,
    close_game_library,
    close_game_library_message,
    menu_hide_app,
    menu_hide_others,
    menu_show_all,
    menu_quit,
    menu_edit,
    menu_undo,
    menu_redo,
    menu_cut,
    menu_copy,
    menu_paste,
    menu_select_all,
    menu_window,
    menu_minimize,
    menu_close,
    tray_show,
    tray_quit,
    pick_data_dir,
    pick_apk,
    key_editor_window_title,
    count
};
[[nodiscard]] OGPLAYCORE_API std::string_view Text(UiLanguage, UiText);
[[nodiscard]] OGPLAYCORE_API std::optional<UiLanguage> ParseUiLanguage(std::string_view);
[[nodiscard]] OGPLAYCORE_API std::string_view UiLanguageTag(UiLanguage);
[[nodiscard]] OGPLAYCORE_API UiLanguage UiLanguageForLocale(std::string_view locale);
[[nodiscard]] OGPLAYCORE_API UiLanguage SystemUiLanguage();
[[nodiscard]] OGPLAYCORE_API UiLanguage ResolveUiLanguage(std::string_view setting);
}  // namespace ogplay::app
