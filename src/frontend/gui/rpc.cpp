#include "ogplay/frontend/gui_rpc.h"
#include "ogplay/frontend/gui_settings.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include "ogplay/core/encoding.h"

namespace ogplay::frontend {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void CheckFields(core::JsonValue params, std::initializer_list<std::string_view> fields) {
    std::size_t found{};
    for (const auto field : fields) if (params.Member(field)) ++found;
    if (params.Size() != found) throw std::invalid_argument("unknown parameter");
}

std::string RequiredString(core::JsonValue params, std::string_view key) {
    const auto member = params.Member(key);
    const auto value = member ? member->String() : std::nullopt;
    if (!value || value->empty() || value->find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string(key) + " requires a nonempty string");
    return std::string(*value);
}

agent::ControlResponse Error(int code, std::string_view message, std::string_view next) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    const auto error = writer.Object();
    writer.AddInteger(error, "code", code);
    writer.AddString(error, "message", message);
    writer.AddString(error, "next_step", next);
    writer.Add(root, "error", error);
    return {false, writer.Serialize(root)};
}

std::string_view Status(LibraryTileStatus status) {
    constexpr std::array names{"damaged", "profile_catalog_unavailable", "missing_profile",
                                "missing_external", "running", "ready"};
    return names.at(static_cast<std::size_t>(status));
}

core::JsonWriter::Value Condition(core::JsonWriter& writer, const LibraryCondition& condition) {
    constexpr std::array names{"ready", "missing", "not_required", "unavailable"};
    const auto value = writer.Object();
    writer.AddString(value, "status", names.at(static_cast<std::size_t>(condition.status)));
    writer.AddString(value, "value", condition.value);
    writer.AddString(value, "detail", condition.detail);
    return value;
}
}  // namespace

GuiRpcService::GuiRpcService(LibraryStore& store, std::filesystem::path cli, GuiRpcHost host)
    : store_(store), cli_(std::move(cli)), host_(std::move(host)),
      adapter_([this](std::string_view method, core::JsonValue params) { return Request(method, params); }) {
    if (!host_.context || !host_.launch || !host_.open_directory)
        throw std::invalid_argument("GUI host callbacks are required");
}

std::string GuiRpcService::Handle(std::string_view request) {
    const auto response = adapter_.Handle(request);
    core::JsonParseError parse_error;
    auto document = core::JsonDocument::ParseStrict(response, parse_error);
    if (!document) throw std::runtime_error("GUI RPC produced invalid JSON");
    const auto error = document->Root().Member("error");
    if (!error || error->Member("next_step")) return response;
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddString(root, "jsonrpc", "2.0");
    writer.Add(root, "id", writer.Copy(*document->Root().Member("id")));
    const auto failure = writer.Object();
    writer.Add(failure, "code", writer.Copy(*error->Member("code")));
    writer.Add(failure, "message", writer.Copy(*error->Member("message")));
    writer.AddString(failure, "next_step", "检查 GUI 请求格式和前端版本后重试。");
    writer.Add(root, "error", failure);
    return writer.Serialize(root);
}

agent::ControlResponse GuiRpcService::Request(std::string_view method, core::JsonValue params) {
    try {
        if (method == "dashboard.list" || method == "dashboard.open") {
            if (method == "dashboard.list") CheckFields(params, {});
            else CheckFields(params, {"installation_id"});
            const auto id = method == "dashboard.open" ? RequiredString(params, "installation_id") : std::string{};
            if (!host_.dashboards) throw std::runtime_error("宿主未接入运行实例查询。");
            const auto dashboards = host_.dashboards();
            core::JsonWriter writer; const auto root = writer.Object(), result = writer.Object();
            if (method == "dashboard.open") {
                const auto found = std::find_if(dashboards.begin(), dashboards.end(), [&](const auto& value) { return value.installation_id == id; });
                if (found == dashboards.end()) return Error(-32004, "实例未运行", "启动游戏后重试。");
                if (found->status != "ready" || !found->port) return Error(-32002, found->detail, "检查 MCP 设置或等待服务启动。");
                if (!host_.open_dashboard) throw std::runtime_error("宿主不支持 Dashboard 窗口。");
                host_.open_dashboard(id); writer.AddBool(result, "opened", true);
            } else {
                const auto items = writer.Array();
                for (const auto& value : dashboards) {
                    const auto item = writer.Object(); writer.AddString(item, "installation_id", value.installation_id);
                    writer.AddUnsignedInteger(item, "process_id", value.process_id);
                    if (value.port) writer.AddUnsignedInteger(item, "port", *value.port); else writer.AddNull(item, "port");
                    writer.AddString(item, "status", value.status); writer.AddString(item, "detail", value.detail); writer.Append(items, item);
                }
                writer.Add(result, "items", items);
            }
            writer.Add(root, "result", result); return {true, writer.Serialize(root)};
        }
        if (method == "game_settings.get" || method == "game_settings.set")
            return GameSettingsRequest(method, params);
        if (method == "settings.get" || method == "settings.set" || method == "settings.open_dir")
            return SettingsRequest(method, params);
        if (method == "dialog.pick" || method == "dialog.poll" || method == "library.analyze" ||
            method == "library.import" || method.starts_with("library.job.") || method.starts_with("library.upload."))
            return ImportRequest(method, params);
        if (method != "library.list" && method != "library.launch" && method != "library.open_dir")
            return Error(-32601, "unknown method", "使用当前版本支持的 GUI 方法。");
        if (method == "library.list") CheckFields(params, {});
        else if (method == "library.launch") CheckFields(params, {"installation_id", "mode"});
        else CheckFields(params, {"installation_id", "kind"});

        // Validate request shape before any library IO or host callback.
        const auto key = method == "library.list" ? std::string{} : RequiredString(params, "installation_id");
        const auto kind = method == "library.open_dir" ? RequiredString(params, "kind") : std::string{};
        if (!kind.empty() && kind != "sandbox" && kind != "log" && kind != "external")
            throw std::invalid_argument("kind must be sandbox, log or external");
        GuiLaunchMode mode = GuiLaunchMode::normal;
        if (method == "library.launch" && params.Member("mode")) {
            const auto value = RequiredString(params, "mode");
            if (value == "preflight") mode = GuiLaunchMode::preflight;
            else if (value == "diagnostic") mode = GuiLaunchMode::diagnostic;
            else if (value != "normal") throw std::invalid_argument("unknown launch mode");
        }
        if (ImportBusy()) return Error(-32002, "正在入库", "入库完成后重试。");
        auto entries = store_.LoadEntries();
        for (auto& entry : entries) {
            try { entry = EffectiveGameEntry(entry, LoadGameSettings(entry.directory)); }
            catch (const std::exception& error) { entry.damage_reason = error.what(); }
        }
        const auto context = host_.context(entries);
        const auto tiles = BuildLibraryTiles(entries, context);
        core::JsonWriter writer;
        const auto root = writer.Object();
        const auto result = writer.Object();
        if (method == "library.list") {
            const auto items = writer.Array();
            for (const auto& tile : tiles) {
                const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.key == tile.key; });
                const auto detail = BuildLibraryDetail(*entry, tile, context);
                const auto item = writer.Object();
                writer.AddString(item, "installation_id", tile.key);
                writer.AddString(item, "display_name", tile.display_name);
                writer.AddString(item, "package", detail.package);
                writer.AddString(item, "version", detail.version);
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
                writer.AddString(item, "detail", tile.detail);
                writer.AddBool(item, "running", tile.running);
                writer.AddBool(item, "can_launch", tile.can_launch);
                writer.AddString(item, "icon", tile.icon_png.empty() ? std::string{} :
                                 "data:image/png;base64," + core::EncodeBase64(tile.icon_png));
                writer.Add(item, "profile", Condition(writer, detail.profile));
                writer.Add(item, "external", Condition(writer, detail.external));
                writer.Append(items, item);
            }
            writer.Add(result, "items", items);
            writer.AddString(result, "library_root", PathUtf8(store_.Root()));
        } else {
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.key == key; });
            if (entry == entries.end()) return Error(-32004, "installation not found", "刷新游戏库后重试。");
            if (method == "library.launch") {
                const auto tile = std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) { return item.key == key; });
                if (!tile->can_launch) return Error(-32001, tile->detail, "检查运行状态、Profile 和数据包目录。");
                const auto config = LoadGuiConfig(store_.Root());
                const auto minimize = std::get<bool>(GuiSetting(config, "minimize_on_launch"));
                if (minimize && !host_.minimize) throw std::runtime_error("宿主不支持最小化窗口。");
                host_.launch(BuildLaunchPlan(cli_, store_.Root(), *entry, config, mode));
                if (minimize) host_.minimize();
                writer.AddString(result, "installation_id", key);
                writer.AddBool(result, "started", true);
            } else {
                auto path = kind == "sandbox" ? LauncherSandboxRoot(store_.Root()) / entry->key : entry->directory;
                if (kind == "external") {
                    if (!entry->metadata || !entry->metadata->external_dir)
                        return Error(-32004, "external directory is not configured", "先为该实例配置数据包目录。");
                    path = *entry->metadata->external_dir;
                }
                if (!std::filesystem::is_directory(path))
                    return Error(-32004, "directory does not exist", "首次启动后再打开沙盒，或检查目录配置。");
                host_.open_directory(path);
                writer.AddString(result, "path", PathUtf8(path));
            }
        }
        writer.Add(root, "result", result);
        return {true, writer.Serialize(root)};
    } catch (const std::invalid_argument& error) {
        return Error(-32602, error.what(), "检查请求参数后重试。");
    } catch (const std::exception& error) {
        return Error(-32000, error.what(), "检查库目录、配置和日志后重试。");
    }
}
}  // namespace ogplay::frontend
