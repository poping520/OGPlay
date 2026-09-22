#include "ogplay/frontend/gui_rpc.h"
#include "ogplay/frontend/gui_settings.h"
#include <limits>
#include <algorithm>
#include <stdexcept>

namespace ogplay::frontend {
namespace {
core::JsonWriter::Value Json(core::JsonWriter& writer, const GuiSettingValue& value) {
    if (const auto flag = std::get_if<bool>(&value)) return writer.Bool(*flag);
    if (const auto number = std::get_if<std::uint32_t>(&value)) return writer.UnsignedInteger(*number);
    return writer.String(std::get<std::string>(value));
}
core::JsonWriter::Value Values(core::JsonWriter& writer, const GuiConfig& config) {
    const auto values = writer.Object();
    for (const auto& field : GuiSettings()) writer.Add(values, field.key, Json(writer, GuiSetting(config, field.key)));
    return values;
}
std::string Revision(const GuiConfig& config) {
    core::JsonWriter writer;
    return writer.Serialize(Values(writer, config));
}
GuiSettingValue ReadValue(core::JsonValue value, const GuiSettingDefinition& field) {
    if (std::holds_alternative<bool>(field.initial) && value.Bool()) return *value.Bool();
    if (std::holds_alternative<std::uint32_t>(field.initial) && value.UnsignedInteger() && *value.UnsignedInteger() <= std::numeric_limits<std::uint32_t>::max())
        return static_cast<std::uint32_t>(*value.UnsignedInteger());
    if (std::holds_alternative<std::string>(field.initial) && value.String()) return std::string(*value.String());
    throw std::invalid_argument(field.key + ": invalid value type");
}
core::JsonWriter::Value FieldList(core::JsonWriter& writer, const std::vector<GuiSettingDefinition>& definitions) {
    const auto fields = writer.Array();
    for (const auto& field : definitions) {
        const auto item = writer.Object(), choices = writer.Array();
        writer.AddString(item, "key", field.key); writer.AddString(item, "group", field.group);
        writer.AddString(item, "label", field.label); writer.AddString(item, "pending", field.pending);
        writer.Add(item, "initial", Json(writer, field.initial)); writer.AddBool(item, "directory", field.directory);
        writer.AddUnsignedInteger(item, "min", field.minimum); writer.AddUnsignedInteger(item, "max", field.maximum);
        for (const auto& choice : field.choices) writer.Append(choices, writer.String(choice));
        writer.Add(item, "choices", choices); writer.Append(fields, item);
    }
    return fields;
}
std::string PathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
}
agent::ControlResponse GuiRpcService::GameSettingsRequest(std::string_view method, core::JsonValue params) {
    const bool save = method == "game_settings.set";
    const auto id = params.Member("installation_id");
    if (!id || !id->String() || id->String()->empty() || params.Size() != (save ? 3U : 1U))
        throw std::invalid_argument("instance settings require installation_id and, for save, values/revision");
    GameSettings replacement;
    std::string expected;
    if (save) {
        const auto values = params.Member("values"), revision = params.Member("revision");
        if (!values || !values->IsObject() || !revision || !revision->String()) throw std::invalid_argument("values/revision required");
        expected = std::string(*revision->String());
        for (const auto& field : GameSettingDefinitions()) if (const auto value = values->Member(field.key)) {
            auto parsed = ReadValue(*value, field); ValidateSettingValue(field, parsed);
            replacement.values[field.key] = std::move(parsed);
        }
        if (replacement.values.size() != values->Size()) throw std::invalid_argument("unknown instance setting");
    }
    if (ImportBusy() || (save && SettingsBusy())) throw std::runtime_error("正在分析或入库，请稍后重试。");
    const auto entries = store_.LoadEntries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.key == *id->String(); });
    if (found == entries.end() || found->Damaged() || !found->metadata) throw std::invalid_argument("installation unavailable");
    const auto& entry = *found;
    const auto global = LoadGuiConfig(store_.Root());
    auto settings = LoadGameSettings(entry.directory);
    const auto revision = [&] {
        core::JsonWriter writer;
        const auto root = writer.Object(), overrides = writer.Object(), inherited = writer.Object();
        writer.AddString(root, "installation_id", entry.key);
        for (const auto& [key, value] : settings.values) writer.Add(overrides, key, Json(writer, value));
        for (const auto& field : GameSettingDefinitions())
            writer.Add(inherited, field.key, Json(writer, EffectiveGameSetting({}, global, entry, field.key)));
        writer.Add(root, "overrides", overrides); writer.Add(root, "inherited", inherited);
        return writer.Serialize(root);
    };
    if (save) {
        if (revision() != expected) throw std::runtime_error("实例或全局设置已变化，请重新载入后保存。");
        ValidateGameSettings(replacement, true);
        if (std::get<bool>(EffectiveGameSetting(replacement, global, entry, "mcp_manual_step")) &&
            !std::get<bool>(EffectiveGameSetting(replacement, global, entry, "mcp_enabled")))
            throw std::invalid_argument("MCP 手动步进需要启用 MCP。");
        SaveGameSettings(entry.directory, replacement);
        settings = std::move(replacement);
    }
    core::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object(), overrides = writer.Object(), inherited = writer.Object();
    writer.AddUnsignedInteger(result, "schema", 1);
    writer.AddString(result, "installation_id", entry.key);
    writer.AddString(result, "display_name", entry.metadata->display_name);
    writer.AddString(result, "profile_id", entry.metadata->profile_id.value_or("未匹配"));
    writer.AddString(result, "revision", revision());
    for (const auto& [key, value] : settings.values) writer.Add(overrides, key, Json(writer, value));
    for (const auto& field : GameSettingDefinitions())
        writer.Add(inherited, field.key, Json(writer, EffectiveGameSetting({}, global, entry, field.key)));
    writer.Add(result, "values", overrides); writer.Add(result, "inherited", inherited);
    writer.Add(result, "fields", FieldList(writer, GameSettingDefinitions()));
    writer.AddString(result, "config_path", PathText(entry.directory / "settings.toml"));
    const auto previews = writer.Object();
    for (const auto& [name, mode] : std::vector<std::pair<std::string, GuiLaunchMode>>{
        {"normal", GuiLaunchMode::normal}, {"preflight", GuiLaunchMode::preflight}, {"diagnostic", GuiLaunchMode::diagnostic}}) {
        const auto preview = writer.Object(), argv = writer.Array();
        try {
            const auto plan = BuildLaunchPlan(cli_, store_.Root(), entry, global, mode);
            for (const auto& arg : plan.argv) writer.Append(argv, writer.String(arg));
            writer.AddString(preview, "error", "");
        } catch (const std::exception& error) { writer.AddString(preview, "error", error.what()); }
        writer.Add(preview, "argv", argv); writer.Add(previews, name, preview);
    }
    writer.Add(result, "previews", previews);
    const auto facts = writer.Object();
    if (host_.game_facts) {
        try { for (const auto& [key, value] : host_.game_facts(entry, EffectiveGameConfig(settings, global))) writer.AddString(facts, key, value); }
        catch (const std::exception& error) { writer.AddString(facts, "Profile 信息不可用", error.what()); }
    }
    writer.Add(result, "facts", facts);
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
agent::ControlResponse GuiRpcService::SettingsRequest(std::string_view method, core::JsonValue params) {
    GuiConfig patch;
    std::string expected;
    if (method == "settings.get") {
        if (params.Size()) throw std::invalid_argument("settings.get accepts no parameters");
    } else if (method == "settings.set") {
        const auto values = params.Member("values"), revision = params.Member("revision");
        if (params.Size() != 2 || !values || !values->IsObject() || !revision || !revision->String())
            throw std::invalid_argument("settings.set requires values and revision");
        expected = std::string(*revision->String());
        std::size_t known{};
        for (const auto& field : GuiSettings()) if (const auto value = values->Member(field.key)) {
            ++known; SetGuiSetting(patch, field.key, ReadValue(*value, field));
        }
        if (known != values->Size()) throw std::invalid_argument("unknown setting");
    } else {
        const auto kind = params.Member("kind");
        if (params.Size() != 1 || !kind || kind->String() != "library") throw std::invalid_argument("only the library directory may be opened");
        host_.open_directory(store_.Root());
        core::JsonWriter writer;
        const auto root = writer.Object(), result = writer.Object();
        writer.AddString(result, "path", PathText(store_.Root())); writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    }
    auto config = LoadGuiConfig(store_.Root());
    if (method == "settings.set") {
        if (SettingsBusy()) throw std::runtime_error("正在分析或入库，请稍后保存设置。");
        if (Revision(config) != expected) throw std::runtime_error("设置已被其他窗口修改，请重新载入后再保存。");
        for (const auto& field : GuiSettings()) if (params.Member("values")->Member(field.key))
            SetGuiSetting(config, field.key, GuiSetting(patch, field.key));
        ValidateGuiConfigDirectories(config);
        SaveGuiConfig(store_.Root(), config);
    }
    core::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    writer.AddUnsignedInteger(result, "schema", 2);
    writer.AddString(result, "revision", Revision(config));
    writer.Add(result, "values", Values(writer, config));
    const auto fields = FieldList(writer, GuiSettings());
    writer.Add(result, "fields", fields);
    writer.AddString(result, "library_root", PathText(store_.Root()));
    writer.AddString(result, "config_path", PathText(store_.Root() / "config.toml"));
    const auto facts = writer.Object();
    if (host_.settings_facts) {
        try {
            for (const auto& [key, value] : host_.settings_facts()) writer.AddString(facts, key, value);
        } catch (const std::exception& error) {
            writer.AddString(facts, "制品信息不可用", error.what());
        }
    }
    writer.Add(result, "facts", facts); writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
}
