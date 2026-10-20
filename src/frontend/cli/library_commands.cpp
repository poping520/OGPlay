#include "library_commands.h"
#include "output.h"
#include "ogplay/app/apk_inspect.h"
#include "ogplay/app/data_hint.h"
#include "ogplay/app/game_run.h"
#include "ogplay/app/host.h"
#include "ogplay/app/json.h"
#include "ogplay/app/library.h"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>

namespace ogplay::cli {
namespace {
using namespace ogplay::app;
std::filesystem::path Path(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("path must not be empty");
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
std::string Text(const std::filesystem::path& path) {
    auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
using Value = JsonWriter::Value;
Value SettingJson(JsonWriter& writer, const SettingValue& value) {
    return std::visit([&](const auto& v) -> Value {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) return writer.Bool(v);
        else if constexpr (std::is_same_v<T, std::uint32_t>) return writer.UnsignedInteger(v);
        else return writer.String(v);
    }, value);
}
Value SettingsJson(JsonWriter& writer, const GameSettings& saved, const LibraryConfig& config, const LibraryEntry& entry) {
    const auto result = writer.Object(), values = writer.Object(), effective = writer.Object(), definitions = writer.Array();
    for (const auto& [key, value] : saved.values) writer.Add(values, key, SettingJson(writer, value));
    for (const auto& field : GameSettingDefinitions()) {
        writer.Add(effective, field.key, SettingJson(writer, EffectiveGameSetting(saved, config, entry, field.key)));
        const auto item = writer.Object();
        writer.AddString(item, "key", field.key); writer.AddString(item, "group", field.group);
        writer.AddString(item, "label", field.label); writer.AddString(item, "pending", field.pending);
        writer.Add(item, "default", SettingJson(writer, field.initial));
        writer.AddUnsignedInteger(item, "minimum", field.minimum); writer.AddUnsignedInteger(item, "maximum", field.maximum);
        const auto choices = writer.Array();
        for (const auto& choice : field.choices) writer.Append(choices, writer.String(choice));
        writer.Add(item, "choices", choices); writer.AddBool(item, "directory", field.directory);
        writer.Append(definitions, item);
    }
    writer.Add(result, "values", values); writer.Add(result, "effective", effective); writer.Add(result, "definitions", definitions);
    return result;
}
SettingValue ParseValue(std::string_view key, std::string_view text) {
    const auto& field = FindGameSetting(key);
    if (std::holds_alternative<bool>(field.initial)) {
        if (text != "true" && text != "false") throw std::invalid_argument(std::string(key) + ": expected true or false");
        return text == "true";
    }
    if (std::holds_alternative<std::uint32_t>(field.initial)) {
        std::uint32_t value{};
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || text.empty()) throw std::invalid_argument(std::string(key) + ": expected an unsigned integer");
        return value;
    }
    return std::string(text);
}
LibraryEntry FindEntry(const LibraryStore& store, std::string_view id) {
    for (const auto& entry : store.LoadEntries()) if (entry.key == id) return entry;
    throw LibraryError(LibraryErrorCode::not_found, "library entry does not exist; run library list to select an installation id");
}
std::string_view Fallback(ApplicationVisualFallback value) {
    constexpr std::string_view names[]{"icon_attribute_missing", "icon_resources_unavailable", "icon_resource_missing", "icon_path_unsupported", "icon_entry_unavailable", "icon_decode_failed", "label_attribute_missing", "label_literal_empty", "label_control_characters", "label_resources_unavailable", "label_resource_missing"};
    return names[static_cast<std::size_t>(value)];
}
Value InspectJson(JsonWriter& writer, const ApkInspection& inspection) {
    const auto result = writer.Object();
    writer.AddString(result, "path", Text(inspection.source_apk));
    writer.AddString(result, "package", inspection.manifest.package);
    writer.AddString(result, "display_name", inspection.display_name);
    writer.AddUnsignedInteger(result, "version_code", inspection.manifest.version_code);
    writer.AddString(result, "version_name", inspection.manifest.version_name.value_or(""));
    if (inspection.manifest.min_sdk) writer.AddUnsignedInteger(result, "min_sdk", *inspection.manifest.min_sdk); else writer.AddNull(result, "min_sdk");
    if (inspection.manifest.target_sdk) writer.AddUnsignedInteger(result, "target_sdk", *inspection.manifest.target_sdk); else writer.AddNull(result, "target_sdk");
    const auto abis = writer.Array(), fallbacks = writer.Array(), notes = writer.Array();
    for (const auto& abi : inspection.abis) writer.Append(abis, writer.String(abi));
    for (const auto fallback : inspection.visual_fallbacks) writer.Append(fallbacks, writer.String(Fallback(fallback)));
    for (const auto& note : inspection.profile_notes) writer.Append(notes, writer.String(note));
    writer.Add(result, "abis", abis); writer.Add(result, "visual_fallbacks", fallbacks); writer.Add(result, "profile_notes", notes);
    writer.AddBool(result, "has_icon", !inspection.icon_png.empty());
    if (inspection.profile) writer.AddString(result, "profile_id", inspection.profile->profile_id); else writer.AddNull(result, "profile_id");
    return result;
}
}

int RunLibraryCommand(std::span<const char* const> args) {
    std::size_t index{};
    auto root = std::optional<std::filesystem::path>{};
    if (index < args.size() && std::string_view(args[index]) == "--library-root") {
        if (++index == args.size()) throw std::invalid_argument("--library-root requires <dir>");
        root = Path(args[index++]);
    }
    if (index == args.size()) throw std::invalid_argument("library requires list|inspect|import|remove|settings|data-hint|run");
    const std::string_view command = args[index++];
    const auto require = [&]() -> std::string_view {
        if (index == args.size()) throw std::invalid_argument("incomplete library " + std::string(command) + " command; see --help");
        return args[index++];
    };
    const auto end = [&] { if (index != args.size()) throw std::invalid_argument("unknown library argument: " + std::string(args[index])); };
    LibraryStore store(root ? *root : app::DefaultLibraryRoot());
    JsonWriter writer;
    Value result;
    if (command == "list") {
        end(); result = writer.Array();
        for (const auto& entry : store.LoadEntries()) {
            const auto item = writer.Object();
            writer.AddString(item, "installation_id", entry.key);
            writer.AddString(item, "directory", Text(entry.directory));
            writer.AddBool(item, "damaged", entry.Damaged());
            if (entry.damage_reason) writer.AddString(item, "damage_reason", *entry.damage_reason);
            if (entry.metadata) {
                writer.AddString(item, "package", entry.metadata->package);
                writer.AddString(item, "display_name", entry.metadata->display_name);
                writer.AddUnsignedInteger(item, "version_code", entry.metadata->version_code);
                writer.AddString(item, "version_name", entry.metadata->version_name);
            }
            writer.Append(result, item);
        }
    } else if (command == "inspect" || command == "import") {
        const auto source = Path(require());
        std::optional<std::filesystem::path> external;
        bool new_instance{}, link_external{};
        if (command == "inspect") end();
        else while (index < args.size()) {
            const auto option = require();
            if (option == "--external-dir" && !external) external = Path(require());
            else if (option == "--new-instance" && !new_instance) new_instance = true;
            else if (option == "--link-external" && !link_external) link_external = true;
            else throw std::invalid_argument("unknown or duplicate import option: " + std::string(option));
        }
        const auto config = LoadLibraryConfig(store.Root());
        const auto profiles = config.profiles_dir ? ProfileCatalog::Load(*config.profiles_dir) : ProfileCatalog::LoadBundled();
        const auto inspection = InspectApk(source, profiles);
        if (command == "inspect") result = InspectJson(writer, inspection);
        else {
            if (link_external && !external) throw std::invalid_argument("--link-external requires --external-dir");
            const auto request = BuildLibraryImport(inspection, external, app::UtcTimestamp(), "/sdcard",
                link_external ? ExternalDataMode::link : ExternalDataMode::copy);
            const auto id = store.Import(request, new_instance);
            result = writer.Object(); writer.AddString(result, "installation_id", id);
            writer.AddString(result, "package", inspection.manifest.package);
        }
    } else if (command == "remove") {
        const auto id = require();
        if (require() != "--yes") throw std::invalid_argument("library remove requires --yes; deletes library-owned data, keeps saves and linked external data");
        bool delete_saves{};
        if (index < args.size() && std::string_view(args[index]) == "--delete-saves") { delete_saves = true; ++index; }
        end(); store.Remove(id, delete_saves);
        result = writer.Object(); writer.AddString(result, "installation_id", id); writer.AddBool(result, "removed", true);
    } else if (command == "data-hint") {
        const std::string id(require());
        std::optional<std::filesystem::path> picked;
        if (index < args.size() && std::string_view(args[index]) == "--external-dir") { ++index; picked = Path(require()); }
        end();
        const auto suggestion = SuggestDataMount(store, id, picked);
        const auto& hint = suggestion.hint;
        const auto strings = [&](const std::vector<std::string>& values) {
            const auto array = writer.Array();
            for (const auto& value : values) writer.Append(array, writer.String(value));
            return array;
        };
        result = writer.Object();
        writer.AddString(result, "kind", hint.kind == DataMountHintKind::remount ? "remount" :
                                          hint.kind == DataMountHintKind::needs_data ? "needs_data" : "none");
        writer.AddString(result, "current_guest_dir", suggestion.current_guest_dir);
        if (suggestion.external_dir) writer.AddString(result, "external_dir", Text(*suggestion.external_dir));
        else writer.AddNull(result, "external_dir");
        writer.AddUnsignedInteger(result, "missing", hint.missing);
        const auto candidates = writer.Array();
        for (const auto& candidate : hint.candidates) {
            const auto item = writer.Object();
            writer.AddString(item, "guest_dir", candidate.guest_dir);
            writer.AddUnsignedInteger(item, "explained", candidate.explained);
            writer.Add(item, "examples", strings(candidate.examples));
            writer.Append(candidates, item);
        }
        writer.Add(result, "candidates", candidates);
        writer.AddString(result, "search_root", hint.search_root);
        writer.Add(result, "examples", strings(hint.examples));
    } else if (command == "settings") {
        const auto action = require(), id = require();
        if (action != "get" && action != "set") throw std::invalid_argument("settings requires get or set");
        GameSettings changes;
        std::vector<std::string> unset;
        std::set<std::string, std::less<>> touched;
        if (action == "get") end();
        else {
            while (index < args.size()) {
                const auto text = require();
                if (text == "--unset") {
                    const std::string key(require());
                    static_cast<void>(FindGameSetting(key));
                    if (!touched.insert(key).second) throw std::invalid_argument("duplicate settings key: " + key);
                    unset.push_back(key);
                } else {
                    const auto equals = text.find('=');
                    if (equals == std::string_view::npos) throw std::invalid_argument("settings set requires key=value or --unset key");
                    const std::string key(text.substr(0, equals));
                    if (!touched.insert(key).second) throw std::invalid_argument("duplicate settings key: " + key);
                    changes.values.emplace(key, ParseValue(key, text.substr(equals + 1)));
                }
            }
            if (touched.empty()) throw std::invalid_argument("settings set requires at least one change");
        }
        const auto entry = FindEntry(store, id);
        if (entry.Damaged()) throw LibraryError(LibraryErrorCode::corrupt_config, "damaged library entry; restore it before changing settings");
        const auto saved = action == "set" ? PatchGameSettings(store, id, changes, unset) : LoadGameSettings(entry.directory);
        result = SettingsJson(writer, saved, LoadLibraryConfig(store.Root()), entry);
    } else if (command == "run") {
        const auto id = require();
        LaunchMode mode = LaunchMode::normal;
        if (index < args.size()) {
            if (require() != "--mode") throw std::invalid_argument("library run accepts --mode normal|preflight|diagnostic");
            const auto value = require();
            if (value == "preflight") mode = LaunchMode::preflight;
            else if (value == "diagnostic") mode = LaunchMode::diagnostic;
            else if (value != "normal") throw std::invalid_argument("unknown launch mode");
        }
        end();
        auto resolved = ResolveLaunch(store, id, mode);
        LogConfig logging; logging.status_to_stderr = true;
        logging.file = resolved.options.apk_path.parent_path() / "last-run.log";
        const auto code = RunGame(std::move(resolved.options), logging);
        if (code != 0) throw std::runtime_error("library run failed; inspect the runtime log");
        result = writer.Object(); writer.AddString(result, "installation_id", id); writer.AddInteger(result, "exit_code", code);
    } else throw std::invalid_argument("unknown library command: " + std::string(command));
    Write(stdout, writer.Serialize(result) + "\n");
    return 0;
}
}
