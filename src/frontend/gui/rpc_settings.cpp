#include "frontend/gui/rpc.h"
#include "frontend/gui/rpc_error.h"
#include "ogplay/app/library.h"
#include "ogplay/app/encoding.h"
#include "ogplay/app/host.h"
#include "ogplay/app/running_icon.h"
#include <array>
#include <fstream>
#include <limits>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <string_view>

namespace ogplay::frontend {
using namespace ogplay::app;

namespace {
// Release builds keep reserved (not yet wired) settings off the pages; the schema itself is unchanged, so
// values, validation and resets still cover every field.
#ifdef NDEBUG
constexpr bool kReservedVisible = false;
#else
constexpr bool kReservedVisible = true;
#endif

// Settings kept off the global and per-game pages until they are polished; comment an entry out to show
// it again. Like reserved ones, they stay in the schema: saved values, validation and launches are unchanged.
constexpr std::array<std::string_view, 3> kHiddenSettings{
    "cartridge",            // 常规 · 主界面卡带
    "supersample",          // 图形 · 默认超采样，游戏设置 · 显示 · 超采样
    "dashboard_auto_open",  // 开发者工具 · 自动打开 Dashboard
};

app::JsonWriter::Value Json(app::JsonWriter& writer, const SettingValue& value) {
    if (const auto flag = std::get_if<bool>(&value)) return writer.Bool(*flag);
    if (const auto number = std::get_if<std::uint32_t>(&value)) return writer.UnsignedInteger(*number);
    return writer.String(std::get<std::string>(value));
}

app::JsonWriter::Value Values(app::JsonWriter& writer, const LibraryConfig& config) {
    const auto values = writer.Object();
    for (const auto& field : SettingDefinitions())
        writer.Add(values, field.key, Json(writer, Setting(config, field.key)));
    return values;
}

std::string Revision(const LibraryConfig& config) {
    app::JsonWriter writer;
    return writer.Serialize(Values(writer, config));
}

SettingValue ReadValue(app::JsonValue value, const SettingDefinition& field) {
    if (std::holds_alternative<bool>(field.initial) && value.Bool()) return *value.Bool();
    if (std::holds_alternative<std::uint32_t>(field.initial) && value.UnsignedInteger() &&
        *value.UnsignedInteger() <= std::numeric_limits<std::uint32_t>::max())
        return static_cast<std::uint32_t>(*value.UnsignedInteger());
    if (std::holds_alternative<std::string>(field.initial) && value.String()) return std::string(*value.String());
    throw std::invalid_argument(field.key + ": invalid value type");
}

app::JsonWriter::Value FieldList(app::JsonWriter& writer, const std::vector<SettingDefinition>& definitions,
                                 const std::span<const std::string_view> hidden = {}) {
    const auto fields = writer.Array();
    for (const auto& field : definitions) {
        const auto item = writer.Object(), choices = writer.Array();
        writer.AddString(item, "key", field.key);
        writer.AddString(item, "group", field.group);
        writer.AddString(item, "label", field.label);
        writer.AddString(item, "pending", field.pending);
        writer.Add(item, "initial", Json(writer, field.initial));
        writer.AddBool(item, "directory", field.directory);
        writer.AddBool(item, "hidden", std::find(hidden.begin(), hidden.end(), field.key) != hidden.end());
        writer.AddUnsignedInteger(item, "min", field.minimum);
        writer.AddUnsignedInteger(item, "max", field.maximum);
        for (const auto& choice : field.choices)
            writer.Append(choices, writer.String(choice));
        writer.Add(item, "choices", choices);
        writer.Append(fields, item);
    }
    return fields;
}

std::string PathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
}  // namespace

app::ConsoleResponse GuiRpcService::GameSettingsRequest(std::string_view method, app::JsonValue params) {
    const bool save = method == "game_settings.set";
    const auto id = params.Member("installation_id");
    if (!id || !id->String() || id->String()->empty() || params.Size() != (save ? 3U : 1U))
        throw std::invalid_argument("instance settings require installation_id and, for save, values/revision");
    GameSettings replacement;
    std::string expected;
    if (save) {
        const auto values = params.Member("values"), revision = params.Member("revision");
        if (!values || !values->IsObject() || !revision || !revision->String())
            throw std::invalid_argument("values/revision required");
        expected = std::string(*revision->String());
        for (const auto& field : GameSettingDefinitions())
            if (const auto value = values->Member(field.key)) {
                auto parsed = ReadValue(*value, field);
                ValidateSettingValue(field, parsed);
                replacement.values[field.key] = std::move(parsed);
            }
        if (replacement.values.size() != values->Size()) throw std::invalid_argument("unknown instance setting");
    }
    if (ImportBusy() || (save && SettingsBusy())) throw std::runtime_error("正在分析或入库，请稍后重试。");
    const auto entries = store_.LoadEntries();
    const auto found =
        std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.key == *id->String(); });
    if (found == entries.end() || found->Damaged() || !found->metadata)
        throw std::invalid_argument("installation unavailable");
    const auto& entry = *found;
    const auto global = LoadLibraryConfig(store_.Root());
    auto settings = LoadGameSettings(entry.directory);
    const auto revision = [&] {
        app::JsonWriter writer;
        const auto root = writer.Object(), overrides = writer.Object(), inherited = writer.Object();
        writer.AddString(root, "installation_id", entry.key);
        for (const auto& [key, value] : settings.values)
            writer.Add(overrides, key, Json(writer, value));
        for (const auto& field : GameSettingDefinitions())
            writer.Add(inherited, field.key, Json(writer, EffectiveGameSetting({}, global, entry, field.key)));
        writer.Add(root, "overrides", overrides);
        writer.Add(root, "inherited", inherited);
        return writer.Serialize(root);
    };
    if (save) {
        if (revision() != expected) throw std::runtime_error("游戏或全局设置已变化，请重新载入后保存。");
        // Only a changed directory must exist now: an unplugged linked pack must not block unrelated edits.
        GameSettings changed;
        for (const auto& [key, value] : replacement.values)
            if (const auto it = settings.values.find(key); it == settings.values.end() || it->second != value)
                changed.values[key] = value;
        ValidateGameSettings(changed, true);
        if (std::get<bool>(EffectiveGameSetting(replacement, global, entry, "mcp_manual_step")) &&
            !std::get<bool>(EffectiveGameSetting(replacement, global, entry, "mcp_enabled")))
            throw std::invalid_argument("MCP 手动步进需要启用 MCP。");
        SaveGameSettings(entry.directory, replacement);
        settings = std::move(replacement);
    }
    app::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object(), overrides = writer.Object(),
               inherited = writer.Object();
    writer.AddUnsignedInteger(result, "schema", 1);
    writer.AddString(result, "installation_id", entry.key);
    writer.AddString(result, "display_name", entry.metadata->display_name);
    if (entry.metadata->profile_id)
        writer.AddString(result, "profile_id", *entry.metadata->profile_id);
    else
        writer.AddNull(result, "profile_id");
    writer.AddString(result, "revision", revision());
    for (const auto& [key, value] : settings.values)
        writer.Add(overrides, key, Json(writer, value));
    for (const auto& field : GameSettingDefinitions())
        writer.Add(inherited, field.key, Json(writer, EffectiveGameSetting({}, global, entry, field.key)));
    writer.Add(result, "values", overrides);
    writer.Add(result, "inherited", inherited);
    writer.Add(result, "fields", FieldList(writer, GameSettingDefinitions(), kHiddenSettings));
    writer.AddBool(result, "reserved_visible", kReservedVisible);
    writer.AddString(result, "config_path", PathText(entry.directory / "settings.toml"));
    const auto previews = writer.Object();
    for (const auto& [name, mode] :
         std::vector<std::pair<std::string, LaunchMode>>{{"normal", LaunchMode::normal},
                                                         {"preflight", LaunchMode::preflight},
                                                         {"diagnostic", LaunchMode::diagnostic}}) {
        const auto preview = writer.Object(), argv = writer.Array();
        try {
            const auto plan = BuildLaunchPlan(game_executable_, store_.Root(), entry, global, mode);
            for (const auto& arg : plan.argv)
                writer.Append(argv, writer.String(arg));
            writer.AddString(preview, "error", "");
        } catch (const std::exception& error) {
            writer.AddString(preview, "error", error.what());
        }
        writer.Add(preview, "argv", argv);
        writer.Add(previews, name, preview);
    }
    writer.Add(result, "previews", previews);
    const auto facts = writer.Object();
    if (host_.game_facts) {
        try {
            for (const auto& [key, value] : host_.game_facts(entry, EffectiveGameConfig(settings, global)))
                writer.AddString(facts, key, value);
        } catch (const std::exception& error) {
            writer.AddString(facts, "profile_error", error.what());
        }
    }
    writer.Add(result, "facts", facts);
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}

namespace {
// The running_icon choices drawn by the same compositor the game uses, from the bundled placeholder game icon.
app::ConsoleResponse IconPreview(const app::JsonValue params) {
    if (params.Size()) throw std::invalid_argument("settings.icon_preview accepts no parameters");
    const auto branding = BundledDataPaths().root / "branding";
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw GuiTextError("图标素材缺失：{path}", {{"path", PathText(path)}});
        std::vector<std::byte> bytes;
        for (char c; file.get(c);)
            bytes.push_back(static_cast<std::byte>(c));
        return bytes;
    };
    const auto game = read(branding / "preview-game.png"), cartridge = read(branding / "ogplay-cartridge.png");
    app::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    for (const auto style : {RunningIconStyle::cradle, RunningIconStyle::badge}) {
        const auto png = EncodeRgbaPng(ComposeRunningIcon(game, cartridge, style, 192));
        if (png.empty()) throw std::runtime_error("预览图标编码失败");
        writer.AddString(result, std::string(RunningIconStyleName(style)),
                         "data:image/png;base64," + EncodeBase64(png));
    }
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
}  // namespace

app::ConsoleResponse GuiRpcService::SettingsRequest(std::string_view method, app::JsonValue params) {
    if (method == "settings.icon_preview") return IconPreview(params);
    LibraryConfig patch;
    std::string expected;
    if (method == "settings.get") {
        if (params.Size()) throw std::invalid_argument("settings.get accepts no parameters");
    } else if (method == "settings.set") {
        const auto values = params.Member("values"), revision = params.Member("revision");
        if (params.Size() != 2 || !values || !values->IsObject() || !revision || !revision->String())
            throw std::invalid_argument("settings.set requires values and revision");
        expected = std::string(*revision->String());
        std::size_t known{};
        for (const auto& field : SettingDefinitions())
            if (const auto value = values->Member(field.key)) {
                ++known;
                SetSetting(patch, field.key, ReadValue(*value, field));
            }
        if (known != values->Size()) throw std::invalid_argument("unknown setting");
    } else {
        const auto kind = params.Member("kind");
        if (params.Size() != 1 || !kind || kind->String() != "library")
            throw std::invalid_argument("only the library directory may be opened");
        host_.open_directory(store_.Root());
        app::JsonWriter writer;
        const auto root = writer.Object(), result = writer.Object();
        writer.AddString(result, "path", PathText(store_.Root()));
        writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    }
    auto config = LoadLibraryConfig(store_.Root());
    if (method == "settings.set") {
        if (SettingsBusy()) throw std::runtime_error("正在分析或入库，请稍后保存设置。");
        if (Revision(config) != expected) throw std::runtime_error("设置已被其他窗口修改，请重新载入后再保存。");
        const auto previous_language = ResolveUiLanguage(std::get<std::string>(Setting(config, "language")));
        const bool showed_demos = std::get<bool>(Setting(config, "show_demo_games"));
        for (const auto& field : SettingDefinitions())
            if (params.Member("values")->Member(field.key)) SetSetting(config, field.key, Setting(patch, field.key));
        ValidateLibraryConfigDirectories(config);
        SaveLibraryConfig(store_.Root(), config);
        const auto language = ResolveUiLanguage(std::get<std::string>(Setting(config, "language")));
        if (language != previous_language && host_.language_changed) host_.language_changed(language);
        // Turning demos back on also restores a demo that was removed.
        if (!showed_demos && std::get<bool>(Setting(config, "show_demo_games")) && host_.seed_demos) host_.seed_demos();
    }
    app::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    writer.AddUnsignedInteger(result, "schema", 2);
    writer.AddString(result, "system_language", UiLanguageTag(SystemUiLanguage()));
    writer.AddString(result, "revision", Revision(config));
    writer.Add(result, "values", Values(writer, config));
    const auto fields = FieldList(writer, SettingDefinitions(), kHiddenSettings);
    writer.Add(result, "fields", fields);
    writer.AddBool(result, "reserved_visible", kReservedVisible);
    writer.AddString(result, "library_root", PathText(store_.Root()));
    writer.AddString(result, "config_path", PathText(store_.Root() / "config.toml"));
    const auto facts = writer.Object();
    if (host_.settings_facts) {
        try {
            for (const auto& [key, value] : host_.settings_facts()) {
                if (value)
                    writer.AddString(facts, key, *value);
                else
                    writer.AddNull(facts, key);
            }
        } catch (const std::exception& error) {
            writer.AddString(facts, "artifacts_error", error.what());
        }
    }
    writer.Add(result, "facts", facts);
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
}  // namespace ogplay::frontend
