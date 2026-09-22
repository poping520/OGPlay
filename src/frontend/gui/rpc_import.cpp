#include "ogplay/frontend/gui_rpc.h"
#include "ogplay/core/encoding.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace ogplay::frontend {
namespace {
constexpr std::uint64_t maximum_bytes = 1024ULL * 1024 * 1024;
std::string Text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path Path(std::string_view text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
void Fields(core::JsonValue params, std::initializer_list<std::string_view> keys) {
    std::size_t found{};
    for (auto key : keys) if (params.Member(key)) ++found;
    if (found != params.Size()) throw std::invalid_argument("unknown parameter");
}
std::string String(core::JsonValue params, std::string_view key) {
    const auto value = params.Member(key);
    const auto text = value ? value->String() : std::nullopt;
    if (!text || text->empty() || text->find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string(key) + " requires a nonempty string");
    return std::string(*text);
}
std::uint64_t Number(core::JsonValue params, std::string_view key) {
    const auto value = params.Member(key);
    const auto number = value ? value->UnsignedInteger() : std::nullopt;
    if (!number) throw std::invalid_argument(std::string(key) + " requires an unsigned integer");
    return *number;
}
void ApkName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!name.ends_with(".apk")) throw std::invalid_argument("当前仅支持单体 APK；请先提取完整 APK，分包安装尚未实现。");
}
template<class T> bool Ready(std::future<T>& value) {
    return value.valid() && value.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}
struct GuiRpcService::ImportState {
    std::uint64_t serial{};
    std::string job, state = "idle", failure, installed;
    std::filesystem::path directory;
    std::uint64_t expected{}, received{};
    std::optional<ApkImportAnalysis> analysis;
    std::future<ApkImportAnalysis> analyzing;
    std::future<std::string> committing;
    std::future<std::optional<std::filesystem::path>> picking;
    std::optional<std::filesystem::path> picked;
    std::string dialog;
    bool dialog_pending{}, cancelled{};
    ~ImportState() {
        // Join before deleting the snapshot or releasing any worker capture.
        if (analyzing.valid()) analyzing.wait();
        if (committing.valid()) committing.wait();
        if (picking.valid()) picking.wait();
        Clean();
    }
    void Clean() {
        if (!directory.empty()) { std::error_code error; std::filesystem::remove_all(directory, error); directory.clear(); }
        analysis.reset();
    }
    void Update() {
        try {
            if (Ready(analyzing)) { analysis = analyzing.get(); state = "ready"; }
            if (Ready(committing)) { installed = committing.get(); state = "imported"; Clean(); }
            if (cancelled && !analyzing.valid()) { Clean(); state = "cancelled"; }
        } catch (const std::exception& error) { failure = error.what(); state = "failed"; Clean(); }
    }
    void Reserve(const std::filesystem::path& root) {
        Update();
        if (analyzing.valid() || committing.valid() || state == "ready" || state == "uploading")
            throw std::runtime_error("已有导入任务，请完成或取消后重试。");
        Clean();
        std::filesystem::create_directories(root);
        for (;;) {
            job = std::to_string(++serial);
            const auto candidate = root / (".gui-import-" + job);
            if (std::filesystem::create_directory(candidate)) { directory = candidate; break; }
        }
        failure.clear(); installed.clear(); cancelled = false; expected = received = 0;
    }
};
bool GuiRpcService::ImportBusy() const {
    return import_ && import_->committing.valid();
}
bool GuiRpcService::SettingsBusy() const {
    return import_ && (import_->analyzing.valid() || import_->committing.valid());
}
agent::ControlResponse GuiRpcService::ImportRequest(std::string_view method, core::JsonValue params) {
    if (!import_) import_ = std::make_shared<ImportState>();
    auto& job = *import_;
    // Schema is checked before polling, IO or host interaction.
    if (method == "dialog.pick") Fields(params, {"kind"});
    else if (method == "dialog.poll") Fields(params, {"dialog"});
    else if (method == "library.analyze") Fields(params, {"path"});
    else if (method == "library.upload.begin") Fields(params, {"name", "size"});
    else if (method == "library.upload.chunk") Fields(params, {"job", "offset", "data"});
    else if (method == "library.import") Fields(params, {"job", "external_dir", "new_instance"});
    else if (method == "library.job.poll" || method == "library.job.cancel" || method == "library.upload.finish") Fields(params, {"job"});
    else throw std::invalid_argument("unknown import method");
    core::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    if (method == "dialog.pick") {
        const auto kind = String(params, "kind");
        if (kind != "file" && kind != "directory") throw std::invalid_argument("kind must be file or directory");
        if (!host_.pick) throw std::runtime_error("文件选择器不可用。");
        if (job.dialog_pending) throw std::runtime_error("请先关闭当前文件选择器。");
        job.picking = host_.pick(kind == "directory");
        job.dialog = std::to_string(++job.serial); job.dialog_pending = true; job.picked.reset();
        writer.AddString(result, "dialog", job.dialog);
    } else if (method == "dialog.poll") {
        if (String(params, "dialog") != job.dialog) throw std::invalid_argument("unknown dialog");
        if (Ready(job.picking)) {
            job.dialog_pending = false;
            job.picked = job.picking.get();
        }
        writer.AddBool(result, "pending", job.dialog_pending);
        if (job.picked) writer.AddString(result, "path", Text(*job.picked)); else writer.AddNull(result, "path");
    } else {
        if (!host_.analyze || !host_.timestamp) throw std::runtime_error("导入服务不可用。");
        if (method == "library.analyze" || method == "library.upload.begin") {
            const auto source = String(params, method == "library.analyze" ? "path" : "name");
            ApkName(source);
            const auto size = method == "library.upload.begin" ? Number(params, "size") : std::filesystem::file_size(Path(source));
            if (size == 0 || size > maximum_bytes) throw std::invalid_argument("APK 必须非空且不超过 1 GiB。");
            job.Reserve(store_.Root());
            const auto snapshot = job.directory / "source.apk";
            if (method == "library.analyze") {
                const auto analyze = host_.analyze;
                job.analyzing = std::async(std::launch::async, [snapshot, source, analyze] {
                    std::filesystem::copy_file(Path(source), snapshot);
                    return analyze(snapshot);
                });
                job.state = "analyzing";
            } else {
                std::ofstream stream(snapshot, std::ios::binary);
                if (!stream) throw std::runtime_error("无法创建导入临时文件。");
                job.expected = size; job.state = "uploading";
            }
        } else {
            if (String(params, "job") != job.job || job.job.empty()) throw std::invalid_argument("unknown or expired job");
            job.Update();
            if (method == "library.job.cancel") {
                if (job.committing.valid() || job.state == "imported") throw std::runtime_error("入库提交后不能取消。");
                job.cancelled = true; job.Update();
            } else if (method == "library.upload.chunk") {
                if (job.state != "uploading") throw std::runtime_error("任务不接受文件分块。");
                const auto offset = Number(params, "offset");
                const auto data = String(params, "data");
                if (data.size() > 262144 || offset != job.received) throw std::invalid_argument("invalid upload chunk size or offset");
                const auto bytes = core::DecodeBase64(data, {.ignore_ascii_whitespace = false});
                if (!bytes || bytes->empty() || bytes->size() > job.expected - job.received) throw std::invalid_argument("invalid upload bytes");
                std::ofstream stream(job.directory / "source.apk", std::ios::binary | std::ios::app);
                stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
                stream.close();
                if (!stream) { job.state = "failed"; job.Clean(); throw std::runtime_error("写入安装包失败，请重新选择。"); }
                job.received += bytes->size();
            } else if (method == "library.upload.finish") {
                if (job.state != "uploading" || job.received != job.expected) throw std::invalid_argument("incomplete upload");
                const auto snapshot = job.directory / "source.apk";
                job.analyzing = std::async(std::launch::async, [snapshot, analyze = host_.analyze] { return analyze(snapshot); });
                job.state = "analyzing";
            } else if (method == "library.import") {
                if (job.state != "ready" || !job.analysis) throw std::runtime_error("分析尚未完成或任务已提交。");
                const auto confirmation = params.Member("new_instance");
                if (!confirmation || confirmation->Bool() != true) throw std::invalid_argument("请确认新建安装实例，不覆盖现有游戏。");
                std::optional<std::filesystem::path> external;
                if (params.Member("external_dir")) external = Path(String(params, "external_dir"));
                const auto request = BuildLibraryImport(*job.analysis, external, host_.timestamp());
                auto store = store_;
                job.committing = std::async(std::launch::async, [store, request]() mutable { return store.Import(request); });
                job.state = "importing";
            }
        }
        writer.AddString(result, "job", job.job);
        writer.AddString(result, "state", job.cancelled ? "cancelled" : job.state);
        if (job.state == "failed") writer.AddString(result, "message", job.failure);
        if (job.state == "imported") writer.AddString(result, "installation_id", job.installed);
        if (job.state == "ready" && job.analysis && !job.cancelled) {
            const auto& analysis = *job.analysis;
            const auto summary = writer.Object();
            writer.AddString(summary, "display_name", analysis.display_name);
            writer.AddString(summary, "package", analysis.manifest.package);
            writer.AddString(summary, "version_name", analysis.manifest.version_name.value_or(""));
            writer.AddUnsignedInteger(summary, "version_code", analysis.manifest.version_code);
            if (analysis.manifest.min_sdk) writer.AddUnsignedInteger(summary, "min_sdk", *analysis.manifest.min_sdk); else writer.AddNull(summary, "min_sdk");
            if (analysis.manifest.target_sdk) writer.AddUnsignedInteger(summary, "target_sdk", *analysis.manifest.target_sdk); else writer.AddNull(summary, "target_sdk");
            const auto abis = writer.Array();
            for (const auto& abi : analysis.abis) writer.Append(abis, writer.String(abi));
            writer.Add(summary, "abis", abis);
            writer.AddString(summary, "icon", analysis.icon_png.empty() ? "" : "data:image/png;base64," + core::EncodeBase64(analysis.icon_png));
            if (analysis.profile) writer.AddString(summary, "profile", analysis.profile->profile_id); else writer.AddNull(summary, "profile");
            if (analysis.profile) writer.AddBool(summary, "requires_external", analysis.profile->requires_external_data); else writer.AddNull(summary, "requires_external");
            const auto entries = store_.LoadEntries();
            writer.AddUnsignedInteger(summary, "existing_instances", std::count_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.metadata && entry.metadata->package == analysis.manifest.package; }));
            writer.Add(result, "summary", summary);
        }
    }
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
} // namespace ogplay::frontend
