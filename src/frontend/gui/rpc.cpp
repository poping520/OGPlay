#include "frontend/gui/rpc.h"
#include "frontend/gui/rpc_error.h"
#include "ogplay/app/library.h"
#include "ogplay/app/run_report.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include "ogplay/app/encoding.h"

namespace ogplay::frontend {
using namespace ogplay::app;

namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void CheckFields(app::JsonValue params, std::initializer_list<std::string_view> fields) {
    std::size_t found{};
    for (const auto field : fields)
        if (params.Member(field)) ++found;
    if (params.Size() != found) throw std::invalid_argument("unknown parameter");
}

std::string RequiredString(app::JsonValue params, std::string_view key) {
    const auto member = params.Member(key);
    const auto value = member ? member->String() : std::nullopt;
    if (!value || value->empty() || value->find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string(key) + " requires a nonempty string");
    return std::string(*value);
}

// message stays diagnostic text; message_id/params name the catalog message the page shows instead, and
// next_step_id what to try next (host.next.*).
app::ConsoleResponse Error(int code, std::string_view text, std::string_view next_id,
                           const app::UiMessage* ui = nullptr) {
    app::JsonWriter writer;
    const auto root = writer.Object();
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", text);
    if (ui) {
        writer.AddString(error, "message_id", ui->id);
        if (!ui->params.empty()) {
            const auto values = writer.Object();
            for (const auto& [key, value] : ui->params)
                writer.AddString(values, key, value);
            writer.Add(error, "params", values);
        }
    }
    writer.AddString(error, "next_step_id", next_id);
    writer.Add(root, "error", error);
    return {false, writer.Serialize(root)};
}

app::ConsoleResponse Error(int code, const app::UiMessage& ui, std::string_view next_id) {
    return Error(code, ui.id, next_id, &ui);
}

app::ConsoleResponse Error(int code, const std::exception& error, std::string_view next_id) {
    return Error(code, error.what(), next_id, app::FindUiMessage(error));
}

std::string_view Status(LibraryTileStatus status) {
    constexpr std::array names{"damaged", "profile_catalog_unavailable", "missing_external", "running", "ready"};
    return names.at(static_cast<std::size_t>(status));
}

app::JsonWriter::Value Condition(app::JsonWriter& writer, const LibraryCondition& condition) {
    constexpr std::array names{"ready", "missing", "not_required", "unavailable"};
    const auto value = writer.Object();
    writer.AddString(value, "status", names.at(static_cast<std::size_t>(condition.status)));
    writer.AddString(value, "value", condition.value);
    writer.AddBool(value, "owned", condition.owned);
    return value;
}

// Bytes of the regular files at or under path, links not followed; nullopt when nothing is there yet.
std::optional<std::uint64_t> StorageBytes(const std::filesystem::path& path) {
    const auto status = std::filesystem::symlink_status(path);
    if (!std::filesystem::exists(status)) return std::nullopt;
    if (std::filesystem::is_regular_file(status)) return std::filesystem::file_size(path);
    if (!std::filesystem::is_directory(status)) return 0U;
    std::uint64_t total{};
    for (const auto& item : std::filesystem::recursive_directory_iterator(
             path, std::filesystem::directory_options::skip_permission_denied))
        if (!item.is_symlink() && item.is_regular_file()) total += item.file_size();
    return total;
}

void AddBytes(app::JsonWriter& writer, const app::JsonWriter::Value object, const std::string_view key,
              const std::optional<std::uint64_t> bytes) {
    if (bytes)
        writer.AddUnsignedInteger(object, key, *bytes);
    else
        writer.AddNull(object, key);
}

}  // namespace

GuiRpcService::GuiRpcService(LibraryStore& store, std::filesystem::path game_executable, GuiRpcHost host,
                             GuiPlatform platform)
    : store_(store), game_executable_(std::move(game_executable)), host_(std::move(host)),
      platform_(std::move(platform)),
      adapter_([this](std::string_view method, app::JsonValue params) { return Request(method, params); }) {
    if (!host_.context || !host_.launch || !host_.open_directory)
        throw std::invalid_argument("GUI host callbacks are required");
}

std::string GuiRpcService::Handle(std::string_view request) {
    const auto response = adapter_.Handle(request);
    app::JsonParseError parse_error;
    auto document =
        app::JsonDocument::ParseStrict(response, parse_error, app::JsonRpcDispatcher::kMaximumResponseBytes);
    if (!document) throw std::runtime_error("GUI RPC produced invalid JSON");
    const auto error = document->Root().Member("error");
    if (!error || error->Member("next_step_id")) return response;
    app::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddString(root, "jsonrpc", "2.0");
    writer.Add(root, "id", writer.Copy(*document->Root().Member("id")));
    const auto failure = writer.Object();
    writer.Add(failure, "code", writer.Copy(*error->Member("code")));
    writer.Add(failure, "message", writer.Copy(*error->Member("message")));
    if (const auto id = error->Member("message_id")) writer.Add(failure, "message_id", writer.Copy(*id));
    if (const auto params = error->Member("params")) writer.Add(failure, "params", writer.Copy(*params));
    writer.AddString(failure, "next_step_id", "host.next.checkRequest");
    writer.Add(root, "error", failure);
    return writer.Serialize(root);
}

app::ConsoleResponse GuiRpcService::Request(std::string_view method, app::JsonValue params) {
    try {
        // A capability this platform leaves out has no methods at all, not a refusing one.
        if (!MethodAvailable(platform_, method)) return Error(-32601, "unknown method", "host.next.useSupportedMethod");
        if (method == "host.features") {
            CheckFields(params, {});
            app::JsonWriter writer;
            const auto root = writer.Object(), result = writer.Object(), features = writer.Object();
            writer.AddString(result, "platform", platform_.name);
            for (const auto& [name, enabled] :
                 std::initializer_list<std::pair<std::string_view, bool>>{{"key_editor", platform_.key_editor},
                                                                          {"dashboard", platform_.dashboard},
                                                                          {"mcp", platform_.mcp},
                                                                          {"launch_modes", platform_.launch_modes},
                                                                          {"open_directory", platform_.open_directory},
                                                                          {"desktop_window", platform_.desktop_window},
                                                                          {"path_settings", platform_.path_settings},
                                                                          {"file_upload", platform_.file_upload},
                                                                          {"developer", platform_.developer}})
                writer.AddBool(features, name, enabled);
            writer.Add(result, "features", features);
            writer.Add(root, "result", result);
            return {true, writer.Serialize(root)};
        }
        if (method == "about.credits" || method == "about.notices") {
            CheckFields(params, method == "about.notices" ? std::initializer_list<std::string_view>{"id"}
                                                          : std::initializer_list<std::string_view>{});
            if (!host_.credits || !host_.credit_notices) throw GuiTextError({"host.error.noCredits"});
            app::JsonWriter writer;
            const auto root = writer.Object(), result = writer.Object();
            if (method == "about.credits") {
                const auto projects = writer.Array();
                for (const auto& project : host_.credits()) {
                    const auto item = writer.Object();
                    writer.AddString(item, "id", project.id);
                    writer.AddString(item, "name", project.name);
                    writer.AddString(item, "version", project.version);
                    writer.AddString(item, "url", project.url);
                    if (!project.original_url.empty()) writer.AddString(item, "original_url", project.original_url);
                    if (project.license)
                        writer.AddString(item, "license", *project.license);
                    else
                        writer.AddNull(item, "license");
                    if (!project.parent.empty()) writer.AddString(item, "parent", project.parent);
                    writer.Append(projects, item);
                }
                writer.Add(result, "projects", projects);
            } else {
                const auto id = RequiredString(params, "id");
                const auto notices = writer.Array();
                for (const auto& [title, text] : host_.credit_notices(id)) {
                    const auto item = writer.Object();
                    writer.AddString(item, "title", title);
                    writer.AddString(item, "text", text);
                    writer.Append(notices, item);
                }
                writer.AddString(result, "id", id);
                writer.Add(result, "notices", notices);
            }
            writer.Add(root, "result", result);
            return {true, writer.Serialize(root)};
        }
        if (method == "about.open_repository") {
            CheckFields(params, {});
            if (!host_.open_url) throw GuiTextError({"host.error.noOpenUrl"});
            host_.open_url(kOgplayRepositoryUrl);
            app::JsonWriter writer;
            const auto root = writer.Object(), result = writer.Object();
            writer.AddBool(result, "opened", true);
            writer.Add(root, "result", result);
            return {true, writer.Serialize(root)};
        }
        if (method == "controls.open") {
            CheckFields(params, {"installation_id"});
            const auto id = RequiredString(params, "installation_id");
            if (!host_.dashboards) throw GuiTextError({"host.error.noRunningQuery"});
            const auto running = host_.dashboards();
            const auto found = std::find_if(running.begin(), running.end(),
                                            [&](const auto& value) { return value.installation_id == id; });
            if (found == running.end())
                return Error(-32004, app::UiMessage{"host.error.gameNotRunning"}, "host.next.startGameFirst");
            if (found->editor_status != "ready")
                return Error(-32002, app::UiMessage{found->editor_detail}, "host.next.waitForStart");
            if (!host_.open_key_editor) throw GuiTextError({"host.error.noKeyEditor"});
            host_.open_key_editor(id);
            app::JsonWriter writer;
            const auto root = writer.Object(), result = writer.Object();
            writer.AddBool(result, "opened", true);
            writer.Add(root, "result", result);
            return {true, writer.Serialize(root)};
        }
        if (method == "dashboard.list" || method == "dashboard.open") {
            if (method == "dashboard.list")
                CheckFields(params, {});
            else
                CheckFields(params, {"installation_id"});
            const auto id = method == "dashboard.open" ? RequiredString(params, "installation_id") : std::string{};
            if (!host_.dashboards) throw GuiTextError({"host.error.noRunningQuery"});
            const auto dashboards = host_.dashboards();
            app::JsonWriter writer;
            const auto root = writer.Object(), result = writer.Object();
            if (method == "dashboard.open") {
                const auto found = std::find_if(dashboards.begin(), dashboards.end(),
                                                [&](const auto& value) { return value.installation_id == id; });
                if (found == dashboards.end())
                    return Error(-32004, app::UiMessage{"host.error.gameNotRunning"}, "host.next.startGame");
                if (found->status != "ready" || !found->port)
                    return Error(-32002, app::UiMessage{found->detail}, "host.next.checkMcp");
                if (!host_.open_dashboard) throw GuiTextError({"host.error.noDashboard"});
                host_.open_dashboard(id);
                writer.AddBool(result, "opened", true);
            } else {
                const auto items = writer.Array();
                for (const auto& value : dashboards) {
                    const auto item = writer.Object();
                    writer.AddString(item, "installation_id", value.installation_id);
                    writer.AddUnsignedInteger(item, "process_id", value.process_id);
                    if (value.port)
                        writer.AddUnsignedInteger(item, "port", *value.port);
                    else
                        writer.AddNull(item, "port");
                    writer.AddString(item, "status", value.status);
                    writer.AddString(item, "detail_id", value.detail);
                    writer.AddString(item, "key_editor_status", value.editor_status);
                    writer.AddString(item, "key_editor_detail_id", value.editor_detail);
                    writer.Append(items, item);
                }
                writer.Add(result, "items", items);
            }
            writer.Add(root, "result", result);
            return {true, writer.Serialize(root)};
        }
        if (method == "game_settings.get" || method == "game_settings.set") return GameSettingsRequest(method, params);
        if (method == "library.data_hint" || method == "library.data_mount.apply")
            return DataHintRequest(method, params);
        if (method == "settings.get" || method == "settings.set" || method == "settings.open_dir" ||
            method == "settings.icon_preview")
            return SettingsRequest(method, params);
        if (method == "dialog.pick" || method == "dialog.poll" || method == "library.analyze" ||
            method == "library.external.inspect" || method == "library.data_pack" || method == "library.import" ||
            method.starts_with("library.job.") || method.starts_with("library.upload."))
            return ImportRequest(method, params);
        if (method != "library.list" && method != "library.launch" && method != "library.open_dir" &&
            method != "library.remove" && method != "library.clear_data" && method != "library.storage" &&
            method != "library.resume" && method != "library.stop")
            return Error(-32601, "unknown method", "host.next.useSupportedMethod");
        if (method == "library.list")
            CheckFields(params, {});
        else if (method == "library.launch")
            CheckFields(params, {"installation_id", "mode"});
        else if (method == "library.remove")
            CheckFields(params, {"installation_id", "confirmed", "delete_saves"});
        else if (method == "library.clear_data")
            CheckFields(params, {"installation_id", "confirmed"});
        else if (method == "library.storage" || method == "library.resume" || method == "library.stop")
            CheckFields(params, {"installation_id"});
        else
            CheckFields(params, {"installation_id", "kind"});

        // Validate request shape before any library IO or host callback.
        const auto key = method == "library.list" ? std::string{} : RequiredString(params, "installation_id");
        const auto kind = method == "library.open_dir" ? RequiredString(params, "kind") : std::string{};
        if (!kind.empty() && kind != "sandbox" && kind != "log" && kind != "external")
            throw std::invalid_argument("kind must be sandbox, log or external");
        LaunchMode mode = LaunchMode::normal;
        if (method == "library.launch" && params.Member("mode")) {
            const auto value = RequiredString(params, "mode");
            if (value == "preflight")
                mode = LaunchMode::preflight;
            else if (value == "diagnostic")
                mode = LaunchMode::diagnostic;
            else if (value != "normal")
                throw std::invalid_argument("unknown launch mode");
            if (mode != LaunchMode::normal && !platform_.launch_modes)
                throw std::invalid_argument("unknown launch mode");
        }
        bool delete_saves{};
        if (method == "library.remove" || method == "library.clear_data") {
            const auto confirmed = params.Member("confirmed");
            if (!confirmed || confirmed->Bool() != std::optional<bool>{true})
                throw std::invalid_argument(method == "library.remove" ? "removal requires confirmed=true"
                                                                       : "clearing data requires confirmed=true");
            if (const auto value = params.Member("delete_saves")) {
                if (!value->Bool()) throw std::invalid_argument("delete_saves requires a boolean");
                delete_saves = *value->Bool();
            }
        }
        if (ImportBusy()) return Error(-32002, app::UiMessage{"host.error.importing"}, "host.next.afterImport");
        auto entries = store_.LoadEntries();
        for (auto& entry : entries) {
            try {
                entry = EffectiveGameEntry(entry, LoadGameSettings(entry.directory));
            }
            // Lock contention is a retryable library state, never entry damage.
            catch (const LibraryError& error) {
                if (error.Code() == LibraryErrorCode::busy) throw;
                entry.damage_reason = error.what();
            } catch (const std::exception& error) {
                entry.damage_reason = error.what();
            }
        }
        const auto demos = host_.demo_packages ? host_.demo_packages() : std::vector<std::string>{};
        const auto is_demo = [&](const LibraryEntry& entry) {
            return entry.metadata && std::find(demos.begin(), demos.end(), entry.metadata->package) != demos.end();
        };
        // Hidden demos leave the list only; their entries, saves and ids stay as they are.
        if (method == "library.list" && !demos.empty() &&
            !std::get<bool>(Setting(LoadLibraryConfig(store_.Root()), "show_demo_games")))
            std::erase_if(entries, is_demo);
        const auto context = host_.context(entries);
        const auto tiles = BuildLibraryTiles(entries, context);
        app::JsonWriter writer;
        const auto root = writer.Object();
        const auto result = writer.Object();
        if (method == "library.list") {
            const auto items = writer.Array();
            for (const auto& tile : tiles) {
                const auto entry = std::find_if(entries.begin(), entries.end(),
                                                [&](const auto& item) { return item.key == tile.key; });
                const auto detail = BuildLibraryDetail(*entry, tile, context);
                const auto item = writer.Object();
                writer.AddString(item, "installation_id", tile.key);
                writer.AddString(item, "display_name", tile.display_name);
                writer.AddString(item, "package", detail.package);
                const auto names = writer.Object();
                if (entry->metadata)
                    for (const auto& [locale, name] : entry->metadata->localized_names)
                        writer.AddString(names, locale, name);
                writer.Add(item, "localized_names", names);
                if (entry->metadata) {
                    writer.AddString(item, "version_name", entry->metadata->version_name);
                    writer.AddUnsignedInteger(item, "version_code", entry->metadata->version_code);
                    writer.AddString(item, "imported_at", entry->metadata->imported_at);
                } else {
                    writer.AddNull(item, "version_name");
                    writer.AddNull(item, "version_code");
                    writer.AddNull(item, "imported_at");
                }
                writer.AddString(item, "sandbox_path", PathUtf8(LauncherSandboxRoot(store_.Root()) / entry->key));
                writer.AddString(item, "log_directory", PathUtf8(entry->directory));
                writer.AddString(item, "status", Status(tile.status));
                if (detail.damage_reason)
                    writer.AddString(item, "damage_reason", *detail.damage_reason);
                else
                    writer.AddNull(item, "damage_reason");
                writer.AddBool(item, "running", tile.running);
                writer.AddBool(item, "can_launch", tile.can_launch);
                writer.AddBool(item, "builtin_demo", is_demo(*entry));
                writer.AddString(item, "icon",
                                 tile.icon_png.empty() ? std::string{}
                                                       : "data:image/png;base64," + app::EncodeBase64(tile.icon_png));
                if (detail.profile)
                    writer.Add(item, "profile", Condition(writer, *detail.profile));
                else
                    writer.AddNull(item, "profile");
                writer.Add(item, "external", Condition(writer, detail.external));
                // GUI-23: the last run, for the attention filter and the hero line.
                if (const auto last_run = LoadLastRunReport(entry->directory / "last-run.json"))
                    writer.Add(item, "last_run", LastRunJson(writer, *last_run, tile.running));
                else
                    writer.AddNull(item, "last_run");
                writer.Append(items, item);
            }
            writer.Add(result, "items", items);
            writer.AddString(result, "library_root", PathUtf8(store_.Root()));
        } else {
            const auto entry =
                std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.key == key; });
            if (entry == entries.end()) return Error(-32004, "installation not found", "host.next.refreshLibrary");
            if (method == "library.remove") {
                const auto tile =
                    std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) { return item.key == key; });
                if (tile->running)
                    return Error(-32002, app::UiMessage{"host.error.gameRunning"}, "host.next.quitBeforeDelete");
                store_.Remove(key, delete_saves);
                // Removing a demo means "no demos": the setting goes off so the next start does not seed it back.
                const bool demo = is_demo(*entry);
                if (demo) {
                    auto config = LoadLibraryConfig(store_.Root());
                    SetSetting(config, "show_demo_games", false);
                    SaveLibraryConfig(store_.Root(), config);
                }
                writer.AddString(result, "installation_id", key);
                writer.AddBool(result, "removed", true);
                writer.AddBool(result, "demo_hidden", demo);
            } else if (method == "library.clear_data") {
                const auto tile =
                    std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) { return item.key == key; });
                if (tile->running)
                    return Error(-32002, app::UiMessage{"host.error.gameRunning"}, "host.next.quitBeforeClear");
                store_.ClearSaves(key);
                writer.AddString(result, "installation_id", key);
                writer.AddBool(result, "cleared", true);
            } else if (method == "library.storage") {
                // Measured on request (the install info dialog), never for the list: data packs can be large.
                const bool external = entry->external_dir.has_value();
                AddBytes(writer, result, "apk_bytes", StorageBytes(LibraryApkPath(entry->directory)));
                AddBytes(writer, result, "external_bytes",
                         external ? StorageBytes(*entry->external_dir) : std::nullopt);
                AddBytes(writer, result, "sandbox_bytes",
                         StorageBytes(LauncherSandboxRoot(store_.Root()) / entry->key));
                if (external)
                    writer.AddString(result, "external_guest_dir", entry->external_guest_dir);
                else
                    writer.AddNull(result, "external_guest_dir");
            } else if (method == "library.resume" || method == "library.stop") {
                const auto tile =
                    std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) { return item.key == key; });
                if (!tile->running)
                    return Error(-32001, app::UiMessage{"host.error.gameNotRunningNow"}, "host.next.refreshLibrary");
                if (!(method == "library.resume" ? host_.resume : host_.stop))
                    throw GuiTextError({"host.error.noSwitch"});
                (method == "library.resume" ? host_.resume : host_.stop)(key);
                writer.AddString(result, "installation_id", key);
            } else if (method == "library.launch") {
                const auto tile =
                    std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) { return item.key == key; });
                // Android runs one game process: a second launch would only bring the running game back.
                if (platform_.single_game)
                    if (const auto other =
                            std::find_if(tiles.begin(), tiles.end(),
                                         [&](const auto& item) { return item.running && item.key != key; });
                        other != tiles.end())
                        return Error(-32003,
                                     app::UiMessage{"host.error.anotherRunning", {{"name", other->display_name}}},
                                     "host.next.backOrClose");
                if (!tile->can_launch)
                    return Error(
                        -32001,
                        app::UiMessage{"host.error.notLaunchable", {{"status", std::string(Status(tile->status))}}},
                        "host.next.checkLaunchState");
                const auto config = LoadLibraryConfig(store_.Root());
                const auto minimize = platform_.desktop_window && std::get<bool>(Setting(config, "minimize_on_launch"));
                if (minimize && !host_.minimize) throw GuiTextError({"host.error.noMinimize"});
                host_.launch(BuildLaunchPlan(game_executable_, store_.Root(), *entry, config, mode, platform_));
                if (minimize) host_.minimize();
                writer.AddString(result, "installation_id", key);
                writer.AddBool(result, "started", true);
            } else {
                auto path = kind == "sandbox" ? LauncherSandboxRoot(store_.Root()) / entry->key : entry->directory;
                if (kind == "external") {
                    if (!entry->external_dir)
                        return Error(-32004, "external directory is not configured", "host.next.configureData");
                    path = *entry->external_dir;
                }
                if (!std::filesystem::is_directory(path))
                    return Error(-32004, "directory does not exist", "host.next.openSandboxLater");
                host_.open_directory(path);
                writer.AddString(result, "path", PathUtf8(path));
            }
        }
        writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    } catch (const LibraryError& error) {
        return Error(error.Code() == LibraryErrorCode::busy || error.Code() == LibraryErrorCode::running ? -32002
                                                                                                         : -32000,
                     error, "host.next.waitLibrary");
    } catch (const std::invalid_argument& error) {
        return Error(-32602, error, "host.next.checkParams");
    } catch (const std::exception& error) {
        return Error(-32000, error, "host.next.checkLibrary");
    }
}
}  // namespace ogplay::frontend
