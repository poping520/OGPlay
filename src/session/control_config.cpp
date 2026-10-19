#include "ogplay/session/control_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>
#include <variant>

#include "ogplay/core/text.h"
#include "ogplay/session/title_profile.h"
#include "title_profile_toml.h"

namespace ogplay::session {
namespace detail {

namespace {

using Table = TomlValue::Table;
using Array = TomlValue::Array;
using input::ControlConfigError;

constexpr std::size_t kMaxControlFileLines = 200;

[[nodiscard]] std::string Field(const std::string_view parent, const std::string_view key) {
    return std::string(parent) + "." + std::string(key);
}

[[nodiscard]] const TomlValue* Find(const Table& table, const std::string_view key) {
    const auto found = table.find(key);
    return found == table.end() ? nullptr : &found->second;
}

[[nodiscard]] const std::string& String(const Table& table, const std::string_view key,
                                        const std::string_view field) {
    const auto* value = Find(table, key);
    const auto* text = value == nullptr ? nullptr : std::get_if<std::string>(&value->value);
    if (text == nullptr || text->empty())
        throw ControlConfigError(Field(field, key) + " must be a non-empty string");
    return *text;
}

[[nodiscard]] std::optional<std::string> OptionalString(const Table& table,
                                                        const std::string_view key,
                                                        const std::string_view field) {
    if (Find(table, key) == nullptr) return std::nullopt;
    return String(table, key, field);
}

[[nodiscard]] std::optional<float> OptionalNumber(const Table& table, const std::string_view key,
                                                  const std::string_view field) {
    const auto* value = Find(table, key);
    if (value == nullptr) return std::nullopt;
    double number = 0;
    if (const auto* integer = std::get_if<std::int64_t>(&value->value)) {
        number = static_cast<double>(*integer);
    } else if (const auto* real = std::get_if<double>(&value->value)) {
        number = *real;
    } else {
        throw ControlConfigError(Field(field, key) + " must be a number");
    }
    if (!std::isfinite(number) || std::abs(number) > 65536)
        throw ControlConfigError(Field(field, key) + " is outside the supported range");
    return static_cast<float>(number);
}

void ExactKeys(const Table& table, const std::string_view field,
               const std::initializer_list<std::string_view> allowed) {
    for (const auto& [key, value] : table) {
        static_cast<void>(value);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw ControlConfigError(std::string(field) + " has unknown field " + key);
    }
}

[[nodiscard]] const Array& Tables(const Table& table, const std::string_view key,
                                  const std::string_view field) {
    static const Array empty;
    const auto* value = Find(table, key);
    if (value == nullptr) return empty;
    const auto* array = std::get_if<Array>(&value->value);
    if (array == nullptr) throw ControlConfigError(Field(field, key) + " must be an array of tables");
    for (const auto& item : *array) {
        if (std::get_if<Table>(&item.value) == nullptr)
            throw ControlConfigError(Field(field, key) + " must be an array of tables");
    }
    return *array;
}

constexpr std::array kAndroidKeyNames{
        std::pair{"back", 4}, std::pair{"dpad_up", 19}, std::pair{"dpad_down", 20},
        std::pair{"dpad_left", 21}, std::pair{"dpad_right", 22}, std::pair{"dpad_center", 23},
        std::pair{"volume_up", 24}, std::pair{"volume_down", 25}, std::pair{"tab", 61},
        std::pair{"space", 62}, std::pair{"enter", 66}, std::pair{"del", 67},
        std::pair{"menu", 82}, std::pair{"search", 84}, std::pair{"button_a", 96},
        std::pair{"button_b", 97}, std::pair{"button_x", 99}, std::pair{"button_y", 100},
        std::pair{"button_l1", 102}, std::pair{"button_r1", 103},
        std::pair{"button_start", 108}, std::pair{"button_select", 109},
        std::pair{"escape", 111},
};

[[nodiscard]] std::int32_t AndroidKeyCode(const TomlValue& value, const std::string_view field) {
    if (const auto* integer = std::get_if<std::int64_t>(&value.value)) {
        if (*integer < 1 || *integer > 255) throw ControlConfigError(std::string(field) + " is out of range");
        return static_cast<std::int32_t>(*integer);
    }
    for (const auto& [name, code] : kAndroidKeyNames) {
        if (const auto* text = std::get_if<std::string>(&value.value); text != nullptr && *text == name)
            return code;
    }
    if (const auto* text = std::get_if<std::string>(&value.value))
        throw ControlConfigError(std::string(field) + " is not a known Android key: " + *text);
    throw ControlConfigError(std::string(field) + " must be a key name or keycode");
}

// "keys:w,a,s,d" expands to four key sources (up, left, down, right).
[[nodiscard]] std::vector<input::InputSource> Sources(const std::string& text) {
    if (!text.starts_with("keys:")) return {input::ParseInputSource(text)};
    std::vector<input::InputSource> sources;
    std::string_view rest = std::string_view{text}.substr(5);
    while (true) {
        const auto comma = rest.find(',');
        const auto name = core::TrimAsciiWhitespace(rest.substr(0, comma));
        sources.push_back(input::ParseInputSource("key:" + std::string(name)));
        if (comma == std::string_view::npos) break;
        rest = rest.substr(comma + 1);
    }
    return sources;
}

[[nodiscard]] input::ZoneSpec DecodeZone(const Table& table, const std::string_view field) {
    ExactKeys(table, field, {"name", "anchor", "x", "y", "w", "h", "r"});
    input::ZoneSpec zone;
    zone.name = String(table, "name", field);
    if (!core::IsValidLowercaseIdentifier(zone.name))
        throw ControlConfigError(Field(field, "name") + " must be a lowercase identifier");
    zone.anchor = input::ParseAnchor(String(table, "anchor", field));
    zone.x = OptionalNumber(table, "x", field).value_or(0);
    zone.y = OptionalNumber(table, "y", field).value_or(0);
    const auto radius = OptionalNumber(table, "r", field);
    const auto width = OptionalNumber(table, "w", field);
    const auto height = OptionalNumber(table, "h", field);
    if (radius.has_value() == (width.has_value() || height.has_value()) ||
        width.has_value() != height.has_value())
        throw ControlConfigError(std::string(field) + " needs either r or both w and h");
    zone.radius = radius.value_or(0);
    zone.width = width.value_or(0);
    zone.height = height.value_or(0);
    return zone;
}

[[nodiscard]] ControlBindingEntry DecodeBinding(const Table& table, const std::string_view field) {
    ControlBindingEntry entry;
    auto& binding = entry.binding;
    const auto& action = String(table, "action", field);
    if (const auto mode = OptionalString(table, "mode", field)) binding.mode = *mode;
    binding.sources = Sources(String(table, "source", field));
    if (action == "none") {
        ExactKeys(table, field, {"source", "action", "mode"});
        entry.unbind = true;
    } else if (action == "button") {
        ExactKeys(table, field, {"source", "action", "mode", "zone", "press", "repeat_frames"});
        binding.action = input::ActionKind::button;
        binding.zone = String(table, "zone", field);
        const auto press = OptionalString(table, "press", field).value_or("hold");
        if (press == "tap") binding.press = input::PressMode::tap;
        else if (press == "hold") binding.press = input::PressMode::hold;
        else if (press == "repeat") binding.press = input::PressMode::repeat;
        else throw ControlConfigError(Field(field, "press") + " must be tap, hold or repeat");
        if (const auto frames = OptionalNumber(table, "repeat_frames", field)) {
            if (*frames < 0 || std::floor(*frames) != *frames)
                throw ControlConfigError(Field(field, "repeat_frames") + " must be a whole number");
            binding.repeat_frames = static_cast<std::uint32_t>(*frames);
        }
    } else if (action == "stick") {
        ExactKeys(table, field, {"source", "action", "mode", "zone", "radius", "settle_frames", "ramp_frames"});
        binding.action = input::ActionKind::stick;
        binding.zone = String(table, "zone", field);
        binding.stick_radius = OptionalNumber(table, "radius", field).value_or(binding.stick_radius);
        for (const auto& [key, target] : {std::pair{"settle_frames", &binding.stick_settle_frames},
                                          std::pair{"ramp_frames", &binding.stick_ramp_frames}}) {
            if (const auto frames = OptionalNumber(table, key, field)) {
                if (*frames < 0 || std::floor(*frames) != *frames)
                    throw ControlConfigError(Field(field, key) + " must be a whole number");
                *target = static_cast<std::uint32_t>(*frames);
            }
        }
    } else if (action == "key") {
        ExactKeys(table, field, {"source", "action", "mode", "key"});
        binding.action = input::ActionKind::key;
        const auto* key = Find(table, "key");
        if (key == nullptr) throw ControlConfigError(Field(field, "key") + " is missing");
        binding.android_key_code = AndroidKeyCode(*key, Field(field, "key"));
    } else if (action == "mode") {
        ExactKeys(table, field, {"source", "action", "mode", "target", "switch"});
        binding.action = input::ActionKind::mode;
        binding.target_mode = String(table, "target", field);
        const auto mode_switch = OptionalString(table, "switch", field).value_or("toggle");
        if (mode_switch == "toggle") binding.mode_switch = input::ModeSwitch::toggle;
        else if (mode_switch == "hold") binding.mode_switch = input::ModeSwitch::hold;
        else throw ControlConfigError(Field(field, "switch") + " must be toggle or hold");
    } else {
        throw ControlConfigError(Field(field, "action") + " is not a known action: " + action);
    }
    if (binding.action != input::ActionKind::stick && !entry.unbind && binding.sources.size() != 1)
        throw ControlConfigError(Field(field, "source") + " must name one key or button");
    return entry;
}

[[nodiscard]] std::vector<input::ZoneSpec> DecodeZones(const Table& table, const std::string_view field) {
    std::vector<input::ZoneSpec> zones;
    const auto& items = Tables(table, "zone", field);
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto zone = DecodeZone(std::get<Table>(items[i].value),
                               Field(field, "zone[" + std::to_string(i) + "]"));
        if (std::ranges::any_of(zones, [&](const auto& z) { return z.name == zone.name; }))
            throw ControlConfigError(std::string(field) + " places zone " + zone.name + " twice");
        zones.push_back(std::move(zone));
    }
    return zones;
}

[[nodiscard]] std::vector<ControlBindingEntry> DecodeBindings(const Table& table,
                                                              const std::string_view field) {
    std::vector<ControlBindingEntry> bindings;
    const auto& items = Tables(table, "binding", field);
    for (std::size_t i = 0; i < items.size(); ++i) {
        bindings.push_back(DecodeBinding(std::get<Table>(items[i].value),
                                         Field(field, "binding[" + std::to_string(i) + "]")));
    }
    return bindings;
}

[[nodiscard]] Table ParseControlFile(const std::string_view text) {
    if (!core::IsValidUtf8(text)) throw ControlConfigError("input configuration is not valid UTF-8");
    if (std::count(text.begin(), text.end(), '\n') >= static_cast<std::ptrdiff_t>(kMaxControlFileLines))
        throw ControlConfigError("input configuration exceeds 200 lines");
    try {
        return ParseDataToml(text);
    } catch (const TitleProfileError& error) {
        throw ControlConfigError(error.what());
    }
}

void RequireSchema(const Table& root) {
    const auto* schema = Find(root, "schema");
    const auto* value = schema == nullptr ? nullptr : std::get_if<std::int64_t>(&schema->value);
    if (value == nullptr || *value != 1) throw ControlConfigError("input configuration schema must be 1");
}

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw ControlConfigError("cannot open input configuration: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool Conflicts(const input::Binding& left, const input::Binding& right) {
    if (left.mode != right.mode && left.mode != input::kEveryMode &&
        right.mode != input::kEveryMode)
        return false;
    return std::ranges::any_of(left.sources, [&](const auto& source) {
        return std::ranges::find(right.sources, source) != right.sources.end();
    });
}

void ApplyBindings(std::vector<input::Binding>& bindings,
                   const std::vector<ControlBindingEntry>& entries) {
    for (const auto& entry : entries) {
        std::erase_if(bindings, [&](const auto& b) { return Conflicts(b, entry.binding); });
        if (!entry.unbind) bindings.push_back(entry.binding);
    }
}

void ApplyZones(std::vector<input::ZoneSpec>& zones, const std::vector<input::ZoneSpec>& layer) {
    for (const auto& zone : layer) {
        const auto found = std::ranges::find_if(zones, [&](const auto& z) { return z.name == zone.name; });
        if (found != zones.end()) *found = zone;
        else zones.push_back(zone);
    }
}

}  // namespace

ControlLayer DecodeControlLayer(const Table& table, const std::string_view field) {
    ExactKeys(table, field, {"zone", "binding"});
    return {DecodeZones(table, field), DecodeBindings(table, field)};
}

}  // namespace detail

ControlLayer LoadControlOverrideText(const std::string_view text) {
    auto root = detail::ParseControlFile(text);
    detail::ExactKeys(root, "override", {"schema", "zone", "binding"});
    detail::RequireSchema(root);
    root.erase("schema");
    return detail::DecodeControlLayer(root, "override");
}

ControlLayer LoadControlOverride(const std::filesystem::path& path) {
    try {
        return LoadControlOverrideText(detail::ReadFile(path));
    } catch (const input::ControlConfigError& error) {
        throw input::ControlConfigError(path.filename().string() + ": " + error.what());
    }
}

input::ControlConfig BuildControlConfig(const ControlLayer& user) {
    auto config = input::DefaultControlConfig();
    detail::ApplyZones(config.zones, user.zones);
    detail::ApplyBindings(config.bindings, user.bindings);
    for (const auto& binding : config.bindings) {
        if (!binding.zone.empty() && !std::ranges::any_of(config.zones,
                [&](const auto& zone) { return zone.name == binding.zone; })) {
            throw input::ControlConfigError("binding uses unplaced zone " + binding.zone);
        }
    }
    return config;
}

namespace {

using FieldValue = std::variant<std::string, std::int64_t, double>;
using Fields = std::vector<std::pair<std::string, FieldValue>>;

[[nodiscard]] FieldValue Number(const float value) {
    const auto cents = std::llround(static_cast<double>(value) * 100.0);
    if (cents % 100 == 0) return static_cast<std::int64_t>(cents / 100);
    return static_cast<double>(cents) / 100.0;
}

[[nodiscard]] std::string NumberText(const double value) {
    const auto cents = std::llround(value * 100.0);
    const auto magnitude = cents < 0 ? -cents : cents;
    std::string text = cents < 0 ? "-" : "";
    text += std::to_string(magnitude / 100) + ".";
    const auto fraction = magnitude % 100;
    text += fraction < 10 ? "0" + std::to_string(fraction) : std::to_string(fraction);
    while (text.back() == '0') text.pop_back();
    return text;
}

[[nodiscard]] std::string SourceText(const input::Binding& binding) {
    if (binding.sources.size() == 1) return input::FormatInputSource(binding.sources[0]);
    std::string text = "keys:";
    for (std::size_t i = 0; i < binding.sources.size(); ++i) {
        const auto name = input::FormatInputSource(binding.sources[i]);
        if (!name.starts_with("key:")) throw input::ControlConfigError("grouped sources must be keys");
        text += (i == 0 ? "" : ",") + name.substr(4);
    }
    return text;
}

[[nodiscard]] Fields ZoneFields(const input::ZoneSpec& zone) {
    Fields fields{{"name", zone.name}, {"anchor", std::string(input::AnchorName(zone.anchor))},
                  {"x", Number(zone.x)}, {"y", Number(zone.y)}};
    if (zone.radius > 0) {
        fields.emplace_back("r", Number(zone.radius));
    } else {
        fields.emplace_back("w", Number(zone.width));
        fields.emplace_back("h", Number(zone.height));
    }
    return fields;
}

[[nodiscard]] Fields BindingFields(const ControlBindingEntry& entry) {
    const auto& binding = entry.binding;
    Fields fields{{"source", SourceText(binding)}};
    if (binding.mode != input::kDefaultMode) fields.emplace_back("mode", binding.mode);
    if (entry.unbind) {
        fields.emplace_back("action", std::string("none"));
        return fields;
    }
    switch (binding.action) {
    case input::ActionKind::button: {
        fields.emplace_back("action", std::string("button"));
        fields.emplace_back("zone", binding.zone);
        constexpr std::array press{"tap", "hold", "repeat"};
        fields.emplace_back("press", std::string(press[static_cast<std::size_t>(binding.press)]));
        if (binding.press == input::PressMode::repeat)
            fields.emplace_back("repeat_frames", static_cast<std::int64_t>(binding.repeat_frames));
        break;
    }
    case input::ActionKind::stick:
        fields.emplace_back("action", std::string("stick"));
        fields.emplace_back("zone", binding.zone);
        fields.emplace_back("radius", Number(binding.stick_radius));
        fields.emplace_back("settle_frames", static_cast<std::int64_t>(binding.stick_settle_frames));
        fields.emplace_back("ramp_frames", static_cast<std::int64_t>(binding.stick_ramp_frames));
        break;
    case input::ActionKind::key: {
        fields.emplace_back("action", std::string("key"));
        FieldValue key = static_cast<std::int64_t>(binding.android_key_code);
        for (const auto& [name, code] : detail::kAndroidKeyNames)
            if (code == binding.android_key_code) key = std::string(name);
        fields.emplace_back("key", key);
        break;
    }
    case input::ActionKind::mode:
        fields.emplace_back("action", std::string("mode"));
        fields.emplace_back("target", binding.target_mode);
        fields.emplace_back("switch", std::string(binding.mode_switch == input::ModeSwitch::hold ? "hold" : "toggle"));
        break;
    }
    return fields;
}

[[nodiscard]] std::string Quoted(const std::string_view text) {
    std::string result = "\"";
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            result += '\\';
            result += character;
        } else if (byte < 0x20 || byte == 0x7f) {
            constexpr char kHex[] = "0123456789ABCDEF";
            result += "\\u00";
            result += kHex[byte >> 4U];
            result += kHex[byte & 0xfU];
        } else {
            result += character;
        }
    }
    return result + "\"";
}

void AppendTomlTable(std::string& text, const std::string_view header, const Fields& fields) {
    text += "\n[[" + std::string(header) + "]]\n";
    for (const auto& [key, value] : fields) {
        text += key + " = ";
        if (const auto* string = std::get_if<std::string>(&value)) text += Quoted(*string);
        else if (const auto* integer = std::get_if<std::int64_t>(&value)) text += std::to_string(*integer);
        else text += NumberText(std::get<double>(value));
        text += "\n";
    }
}

[[nodiscard]] core::JsonWriter::Value FieldsJson(core::JsonWriter& writer, const Fields& fields) {
    const auto object = writer.Object();
    for (const auto& [key, value] : fields) {
        if (const auto* string = std::get_if<std::string>(&value)) writer.AddString(object, key, *string);
        else if (const auto* integer = std::get_if<std::int64_t>(&value)) writer.AddInteger(object, key, *integer);
        else writer.AddReal(object, key, std::get<double>(value));
    }
    return object;
}

[[nodiscard]] detail::TomlValue JsonToToml(const core::JsonValue value, const std::size_t depth) {
    if (depth > 8) throw input::ControlConfigError("input configuration JSON is nested too deeply");
    if (value.IsObject()) {
        detail::TomlValue::Table table;
        for (const auto key : value.Keys())
            table.emplace(std::string(key), JsonToToml(*value.Member(key), depth + 1));
        return {std::move(table)};
    }
    if (value.IsArray()) {
        detail::TomlValue::Array array;
        for (std::size_t i = 0; i < value.Size(); ++i) array.push_back(JsonToToml(*value.Element(i), depth + 1));
        return {std::move(array)};
    }
    if (const auto text = value.String()) return {std::string(*text)};
    if (const auto flag = value.Bool()) return {*flag};
    if (const auto integer = value.Integer()) return {*integer};
    if (const auto number = value.Number()) return {*number};
    throw input::ControlConfigError("input configuration JSON holds an unsupported value");
}

}  // namespace

ControlLayer LoadControlOverrideIfPresent(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) return {};
    return LoadControlOverride(path);
}

std::string FormatControlLayer(const ControlLayer& layer) {
    std::string text = "# OGPlay key layout, maintained by the key editor (ADR-0134).\nschema = 1\n";
    for (const auto& zone : layer.zones) AppendTomlTable(text, "zone", ZoneFields(zone));
    for (const auto& entry : layer.bindings) AppendTomlTable(text, "binding", BindingFields(entry));
    return text;
}

void SaveControlLayer(const std::filesystem::path& path, const ControlLayer& layer) {
    const auto text = FormatControlLayer(layer);
    static_cast<void>(LoadControlOverrideText(text));
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << text;
        if (!output.flush()) throw input::ControlConfigError("cannot write input configuration: " + temporary.string());
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        throw input::ControlConfigError("cannot replace input configuration: " + path.string());
    }
}

ControlLayer DecodeControlLayerJson(const core::JsonValue value) {
    if (!value.IsObject()) throw input::ControlConfigError("input configuration JSON must be an object");
    const auto table = JsonToToml(value, 0);
    return detail::DecodeControlLayer(std::get<detail::TomlValue::Table>(table.value), "layer");
}

core::JsonWriter::Value WriteControlLayerJson(core::JsonWriter& writer, const ControlLayer& layer) {
    const auto object = writer.Object();
    const auto zones = writer.Array();
    for (const auto& zone : layer.zones) writer.Append(zones, FieldsJson(writer, ZoneFields(zone)));
    writer.Add(object, "zone", zones);
    const auto bindings = writer.Array();
    for (const auto& entry : layer.bindings) writer.Append(bindings, FieldsJson(writer, BindingFields(entry)));
    writer.Add(object, "binding", bindings);
    return object;
}

}  // namespace ogplay::session
