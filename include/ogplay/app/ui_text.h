#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include "ogplay/app/export.h"

namespace ogplay::app {
enum class UiLanguage : std::uint8_t { zh_CN, en_US };
enum class UiText : std::uint16_t {
    close_game_title,
    close_game_message,
    close_game_confirm,
    close_game_cancel,
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
