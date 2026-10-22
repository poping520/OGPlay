#include "frontend/gui/rpc.h"
#include "ogplay/app/data_hint.h"
#include "ogplay/app/encoding.h"
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
void Fields(app::JsonValue params, std::initializer_list<std::string_view> keys) {
    std::size_t found{};
    for (auto key : keys) if (params.Member(key)) ++found;
    if (found != params.Size()) throw std::invalid_argument("unknown parameter");
}
std::string String(app::JsonValue params, std::string_view key) {
    const auto value = params.Member(key);
    const auto text = value ? value->String() : std::nullopt;
    if (!text || text->empty() || text->find('\0') != std::string_view::npos)
        throw std::invalid_argument(std::string(key) + " requires a nonempty string");
    return std::string(*text);
}
std::uint64_t Number(app::JsonValue params, std::string_view key) {
    const auto value = params.Member(key);
    const auto number = value ? value->UnsignedInteger() : std::nullopt;
    if (!number) throw std::invalid_argument(std::string(key) + " requires an unsigned integer");
    return *number;
}
void ApkName(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!name.ends_with(".apk")) throw std::invalid_argument("当前仅支持单体 APK；请先提取完整 APK，分包安装尚未实现。");
}
// Summarizes a candidate data directory for the mount preview: a few top-level
// names plus bounded totals, so huge trees cannot stall the RPC thread.
void DescribeDataDirectory(app::JsonWriter& writer, app::JsonWriter::Value result, const std::filesystem::path& path) {
    constexpr std::size_t shown_entries = 8, scan_limit = 200000;
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) throw std::invalid_argument("数据包目录不可用。");
    std::vector<std::pair<bool, std::string>> top;
    for (std::filesystem::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
        top.emplace_back(!it->is_directory(error), Text(it->path().filename()));
    if (error) throw std::runtime_error("无法读取数据包目录。");
    std::sort(top.begin(), top.end());
    std::uint64_t files{}, bytes{}, scanned{};
    bool truncated{};
    for (std::filesystem::recursive_directory_iterator it(path, error), end; !error && it != end; it.increment(error)) {
        if (++scanned > scan_limit) { truncated = true; break; }
        if (it->is_regular_file(error)) { ++files; bytes += it->file_size(error); }
    }
    if (error) throw std::runtime_error("无法统计数据包目录。");
    writer.AddString(result, "path", Text(std::filesystem::absolute(path).lexically_normal()));
    const auto entries = writer.Array();
    for (std::size_t index = 0; index < top.size() && index < shown_entries; ++index) {
        const auto entry = writer.Object();
        writer.AddString(entry, "name", top[index].second);
        writer.AddBool(entry, "directory", !top[index].first);
        writer.Append(entries, entry);
    }
    writer.Add(result, "entries", entries);
    writer.AddUnsignedInteger(result, "entry_count", top.size());
    writer.AddUnsignedInteger(result, "files", files);
    writer.AddUnsignedInteger(result, "bytes", bytes);
    writer.AddBool(result, "truncated", truncated);
}
template<class T> bool Ready(std::future<T>& value) {
    return value.valid() && value.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}
struct GuiRpcService::ImportState {
    std::uint64_t serial{};
    std::string job, state = "idle", failure, installed;
    std::optional<LibraryErrorCode> failure_code;
    std::filesystem::path directory;
    std::uint64_t expected{}, received{};
    std::optional<ApkImportAnalysis> analysis;
    std::future<ApkImportAnalysis> analyzing;
    std::future<std::string> committing;
    std::shared_ptr<ImportProgress> progress;
    std::future<std::optional<std::filesystem::path>> picking;
    std::optional<std::filesystem::path> picked;
    std::string dialog;
    bool dialog_pending{}, cancelled{};
    ~ImportState() {
        // Join before deleting the snapshot or releasing any worker capture.
        if (progress) progress->cancel_requested = true;
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
            // A cancel that arrives after publication loses; the import stands.
            if (Ready(committing)) { installed = committing.get(); state = "imported"; cancelled = false; Clean(); }
            if (cancelled && !analyzing.valid() && !committing.valid()) { Clean(); state = "cancelled"; }
        } catch (const LibraryError& error) {
            if (cancelled) { Clean(); state = "cancelled"; return; }
            failure_code = error.Code(); failure = error.what(); state = "failed"; Clean();
        } catch (const std::exception& error) {
            if (cancelled) { Clean(); state = "cancelled"; return; }
            failure = error.what(); state = "failed"; Clean();
        }
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
        failure_code.reset(); failure.clear(); installed.clear(); cancelled = false; expected = received = 0;
    }
};
bool GuiRpcService::ImportBusy() const {
    return import_ && import_->committing.valid();
}
bool GuiRpcService::SettingsBusy() const {
    return import_ && (import_->analyzing.valid() || import_->committing.valid());
}
app::ConsoleResponse GuiRpcService::ImportRequest(std::string_view method, app::JsonValue params) {
    if (!import_) import_ = std::make_shared<ImportState>();
    auto& job = *import_;
    // Schema is checked before polling, IO or host interaction.
    if (method == "dialog.pick") Fields(params, {"kind"});
    else if (method == "dialog.poll") Fields(params, {"dialog"});
    else if (method == "library.analyze") Fields(params, {"path"});
    else if (method == "library.external.inspect") Fields(params, {"path", "package"});
    else if (method == "library.upload.begin") Fields(params, {"name", "size"});
    else if (method == "library.upload.chunk") Fields(params, {"job", "offset", "data"});
    else if (method == "library.import") Fields(params, {"job", "external_dir", "external_guest_dir", "external_mode", "new_instance"});
    else if (method == "library.job.poll" || method == "library.job.cancel" || method == "library.upload.finish") Fields(params, {"job"});
    else throw std::invalid_argument("unknown import method");
    app::JsonWriter writer;
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
    } else if (method == "library.external.inspect") {
        const auto path = Path(String(params, "path"));
        DescribeDataDirectory(writer, result, path);
        const auto guest = app::GuessDataMount(path, params.Member("package") ? String(params, "package") : std::string{});
        if (guest) writer.AddString(result, "suggested_guest_dir", *guest); else writer.AddNull(result, "suggested_guest_dir");
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
                if (job.state == "imported") throw std::runtime_error("已加入游戏库，不能取消。");
                // Copying data honors the request; a finished APK-only commit may still win.
                if (job.committing.valid() && job.progress) job.progress->cancel_requested = true;
                job.cancelled = true; job.Update();
            } else if (method == "library.upload.chunk") {
                if (job.state != "uploading") throw std::runtime_error("任务不接受文件分块。");
                const auto offset = Number(params, "offset");
                const auto data = String(params, "data");
                if (data.size() > 262144 || offset != job.received) throw std::invalid_argument("invalid upload chunk size or offset");
                const auto bytes = app::DecodeBase64(data, {.ignore_ascii_whitespace = false});
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
                if (!confirmation || confirmation->Bool() != true) throw std::invalid_argument("请确认另外添加一份，不覆盖现有游戏。");
                std::optional<std::filesystem::path> external;
                if (params.Member("external_dir")) external = Path(String(params, "external_dir"));
                auto mode = ExternalDataMode::copy;
                if (params.Member("external_mode")) {
                    const auto text = String(params, "external_mode");
                    if (text != "copy" && text != "link") throw std::invalid_argument("external_mode must be copy or link");
                    mode = text == "link" ? ExternalDataMode::link : ExternalDataMode::copy;
                }
                const auto request = BuildLibraryImport(*job.analysis, external, host_.timestamp(),
                    params.Member("external_guest_dir") ? String(params, "external_guest_dir") : "/sdcard", mode);
                auto store = store_;
                job.progress = std::make_shared<ImportProgress>();
                job.committing = std::async(std::launch::async, [store, request, progress = job.progress]() mutable {
                    return store.Import(request, true, progress.get());
                });
                job.state = "importing";
            }
        }
        if (job.state == "failed" && (job.failure_code == LibraryErrorCode::busy || job.failure_code == LibraryErrorCode::running))
            throw LibraryError(*job.failure_code, job.failure);
        writer.AddString(result, "job", job.job);
        // A commit stays "importing" until the worker has actually stopped.
        writer.AddString(result, "state", job.cancelled && !job.committing.valid() ? "cancelled" : job.state);
        if (job.state == "failed") writer.AddString(result, "message", job.failure);
        if (job.state == "imported") writer.AddString(result, "installation_id", job.installed);
        if (job.state == "importing" && job.progress) {
            const auto progress = writer.Object();
            writer.AddUnsignedInteger(progress, "copied_bytes", job.progress->copied_bytes.load(std::memory_order_relaxed));
            writer.AddUnsignedInteger(progress, "total_bytes", job.progress->total_bytes.load(std::memory_order_relaxed));
            writer.Add(result, "progress", progress);
        }
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
            writer.AddString(summary, "icon", analysis.icon_png.empty() ? "" : "data:image/png;base64," + app::EncodeBase64(analysis.icon_png));
            if (analysis.profile) writer.AddString(summary, "profile", analysis.profile->profile_id); else writer.AddNull(summary, "profile");
            const auto entries = store_.LoadEntries();
            writer.AddUnsignedInteger(summary, "existing_instances", static_cast<std::uint64_t>(std::count_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.metadata && entry.metadata->package == analysis.manifest.package; })));
            writer.Add(result, "summary", summary);
        }
    }
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
} // namespace ogplay::frontend
