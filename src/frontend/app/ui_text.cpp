#include "ogplay/app/ui_text.h"
#include "ogplay/hal/host_environment.h"
#include <array>
#include <stdexcept>

namespace ogplay::app {
namespace {
struct Entry {
    UiText id;
    std::string_view zh, en;
};

// clang-format off: keep both translations together on each row.
constexpr auto kText = std::to_array<Entry>({
    {UiText::close_game_title, "关闭游戏", "Close Game"},
    {UiText::close_game_message, "确定要关闭游戏吗？未保存的进度可能会丢失。", "Close the game? Unsaved progress may be lost."},
    {UiText::close_game_confirm, "关闭", "Close"},
    {UiText::close_game_cancel, "取消", "Cancel"},
    {UiText::menu_hide_app, "隐藏 OGPlay", "Hide OGPlay"},
    {UiText::menu_hide_others, "隐藏其他", "Hide Others"},
    {UiText::menu_show_all, "全部显示", "Show All"},
    {UiText::menu_quit, "退出 OGPlay", "Quit OGPlay"},
    {UiText::menu_edit, "编辑", "Edit"},
    {UiText::menu_undo, "撤销", "Undo"},
    {UiText::menu_redo, "重做", "Redo"},
    {UiText::menu_cut, "剪切", "Cut"},
    {UiText::menu_copy, "拷贝", "Copy"},
    {UiText::menu_paste, "粘贴", "Paste"},
    {UiText::menu_select_all, "全选", "Select All"},
    {UiText::menu_window, "窗口", "Window"},
    {UiText::menu_minimize, "最小化", "Minimize"},
    {UiText::menu_close, "关闭", "Close"},
    {UiText::tray_show, "显示 OGPlay", "Show OGPlay"},
    {UiText::tray_quit, "退出 OGPlay", "Quit OGPlay"},
    {UiText::pick_data_dir, "选择游戏数据包目录", "Choose Game Data Folder"},
    {UiText::pick_apk, "选择 APK", "Choose APK"},
    {UiText::key_editor_window_title, "OGPlay 按键设置", "OGPlay Key Settings"},
});
// clang-format on

consteval bool Complete() {
    if (kText.size() != static_cast<std::size_t>(UiText::count)) return false;
    for (std::size_t i = 0; i < kText.size(); ++i)
        if (static_cast<std::size_t>(kText[i].id) != i || kText[i].zh.empty() || kText[i].en.empty()) return false;
    return true;
}

static_assert(Complete());
}  // namespace

std::string_view Text(UiLanguage language, UiText id) {
    const auto& row = kText.at(static_cast<std::size_t>(id));
    switch (language) {
    case UiLanguage::zh_CN: return row.zh;
    case UiLanguage::en_US: return row.en;
    }
    throw std::invalid_argument("unknown UI language");
}

std::optional<UiLanguage> ParseUiLanguage(std::string_view text) {
    if (text == "zh-CN") return UiLanguage::zh_CN;
    if (text == "en-US") return UiLanguage::en_US;
    return std::nullopt;
}

std::string_view UiLanguageTag(UiLanguage language) {
    switch (language) {
    case UiLanguage::zh_CN: return "zh-CN";
    case UiLanguage::en_US: return "en-US";
    }
    throw std::invalid_argument("unknown UI language");
}

UiLanguage UiLanguageForLocale(std::string_view locale) {
    const auto base = locale.substr(0, locale.find_first_of("-_"));
    return base == "zh" || base == "ZH" || base == "Zh" || base == "zH" ? UiLanguage::zh_CN : UiLanguage::en_US;
}

UiLanguage SystemUiLanguage() {
    return UiLanguageForLocale(hal::HostPreferredLocale());
}

UiLanguage ResolveUiLanguage(std::string_view setting) {
    if (setting == "system") return SystemUiLanguage();
    if (const auto value = ParseUiLanguage(setting)) return *value;
    throw std::invalid_argument("language must be system, zh-CN or en-US");
}
}  // namespace ogplay::app
