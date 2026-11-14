#include "frontend/gui/rpc.h"
#include "ogplay/app/data_hint.h"
#include "ogplay/app/encoding.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <map>
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
    for (auto key : keys)
        if (params.Member(key)) ++found;
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
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!name.ends_with(".apk"))
        throw std::invalid_argument("当前仅支持单体 APK；请先提取完整 APK，分包安装尚未实现。");
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
        if (++scanned > scan_limit) {
            truncated = true;
            break;
        }
        if (it->is_regular_file(error)) {
            ++files;
            bytes += it->file_size(error);
        }
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

// The same summary as DescribeDataDirectory for files that are not on disk yet (an archive's data, a SAF
// directory): top-level names under prefix ("" for all) and totals.
void DescribeFiles(app::JsonWriter& writer, app::JsonWriter::Value result,
                   const std::vector<std::pair<std::string, std::uint64_t>>& files) {
    constexpr std::size_t shown_entries = 8;
    std::map<std::string, bool> top;   // name -> directory
    std::uint64_t bytes{};
    for (const auto& [path, size] : files) {
        const auto slash = path.find('/');
        auto& directory = top[path.substr(0, slash)];
        directory = directory || slash != std::string::npos;
        bytes += size;
    }
    std::vector<std::pair<bool, std::string>> sorted;
    for (const auto& [name, directory] : top) sorted.emplace_back(!directory, name);
    std::sort(sorted.begin(), sorted.end());
    const auto entries = writer.Array();
    for (std::size_t index = 0; index < sorted.size() && index < shown_entries; ++index) {
        const auto entry = writer.Object();
        writer.AddString(entry, "name", sorted[index].second);
        writer.AddBool(entry, "directory", !sorted[index].first);
        writer.Append(entries, entry);
    }
    writer.Add(result, "entries", entries);
    writer.AddUnsignedInteger(result, "entry_count", sorted.size());
    writer.AddUnsignedInteger(result, "files", files.size());
    writer.AddUnsignedInteger(result, "bytes", bytes);
    writer.AddBool(result, "truncated", false);
}

// An archive's planned data relative to a mount ("/sdcard/..."), for the preview.
std::vector<std::pair<std::string, std::uint64_t>> Under(const ImportSource& source, const ArchivePlan& plan,
                                                         std::string_view mount) {
    std::vector<std::pair<std::string, std::uint64_t>> files;
    for (const auto& item : plan.data)
        if (item.guest_path.size() > mount.size() && item.guest_path.starts_with(mount) && item.guest_path[mount.size()] == '/')
            files.emplace_back(item.guest_path.substr(mount.size() + 1), source.Files()[item.index].size);
    return files;
}

template <class T>
bool Ready(std::future<T>& value) {
    return value.valid() && value.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}  // namespace

// What analysis found: the APK, and for a game archive the archive itself, its plan and the APK taken from it.
struct Analyzed final {
    ApkImportAnalysis analysis;
    std::shared_ptr<ImportSource> archive;
    ArchivePlan plan;
    std::size_t apk{};
};

struct GuiRpcService::ImportState {
    std::uint64_t serial{};
    std::string job, state = "idle", failure, installed;
    std::optional<LibraryErrorCode> failure_code;
    std::unique_ptr<ImportWorkDirectory> work;   // the job's snapshot and extracted data, swept if the process dies
    std::filesystem::path directory;
    std::uint64_t expected{}, received{};
    std::optional<ApkImportAnalysis> analysis;
    std::shared_ptr<ImportSource> archive;
    ArchivePlan plan;
    std::size_t apk{};
    std::future<Analyzed> analyzing;
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
        work.reset();
        directory.clear();
        analysis.reset();
        archive.reset();
        plan = {};
    }

    void Update() {
        try {
            if (Ready(analyzing)) {
                auto value = analyzing.get();
                analysis = std::move(value.analysis);
                archive = std::move(value.archive);
                plan = std::move(value.plan);
                apk = value.apk;
                state = "ready";
            }
            // A cancel that arrives after publication loses; the import stands.
            if (Ready(committing)) {
                installed = committing.get();
                state = "imported";
                cancelled = false;
                Clean();
            }
            if (cancelled && !analyzing.valid() && !committing.valid()) {
                Clean();
                state = "cancelled";
            }
        } catch (const LibraryError& error) {
            if (cancelled) {
                Clean();
                state = "cancelled";
                return;
            }
            failure_code = error.Code();
            failure = error.what();
            state = "failed";
            Clean();
        } catch (const std::exception& error) {
            if (cancelled) {
                Clean();
                state = "cancelled";
                return;
            }
            failure = error.what();
            state = "failed";
            Clean();
        }
    }

    void Reserve(const std::filesystem::path& root) {
        Update();
        if (analyzing.valid() || committing.valid() || state == "ready" || state == "uploading")
            throw std::runtime_error("已有导入任务，请完成或取消后重试。");
        Clean();
        work = std::make_unique<ImportWorkDirectory>(root);
        directory = work->Path();
        job = std::to_string(++serial);
        failure_code.reset();
        failure.clear();
        installed.clear();
        cancelled = false;
        expected = received = 0;
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
    if (method == "dialog.pick")
        Fields(params, {"kind"});
    else if (method == "dialog.poll")
        Fields(params, {"dialog"});
    else if (method == "library.analyze")
        Fields(params, {"path", "apk"});
    else if (method == "library.external.inspect")
        Fields(params, {"path", "package"});
    else if (method == "library.upload.begin")
        Fields(params, {"name", "size"});
    else if (method == "library.upload.chunk")
        Fields(params, {"job", "offset", "data"});
    else if (method == "library.import")
        Fields(params, {"job", "external_dir", "external_guest_dir", "external_mode", "new_instance", "extras"});
    else if (method == "library.job.poll" || method == "library.job.cancel" || method == "library.upload.finish")
        Fields(params, {"job"});
    else
        throw std::invalid_argument("unknown import method");
    app::JsonWriter writer;
    const auto root = writer.Object(), result = writer.Object();
    if (method == "dialog.pick") {
        const auto kind = String(params, "kind");
        if (kind != "file" && kind != "directory") throw std::invalid_argument("kind must be file or directory");
        if (!host_.pick) throw std::runtime_error("文件选择器不可用。");
        if (job.dialog_pending) throw std::runtime_error("请先关闭当前文件选择器。");
        job.picking = host_.pick(kind == "directory");
        job.dialog = std::to_string(++job.serial);
        job.dialog_pending = true;
        job.picked.reset();
        writer.AddString(result, "dialog", job.dialog);
    } else if (method == "dialog.poll") {
        if (String(params, "dialog") != job.dialog) throw std::invalid_argument("unknown dialog");
        if (Ready(job.picking)) {
            job.dialog_pending = false;
            job.picked = job.picking.get();
        }
        writer.AddBool(result, "pending", job.dialog_pending);
        if (job.picked) {
            writer.AddString(result, "path", Text(*job.picked));
            const auto label = host_.label ? host_.label(*job.picked) : std::string{};
            writer.AddString(result, "label", label.empty() ? Text(*job.picked) : label);
        } else {
            writer.AddNull(result, "path");
        }
    } else if (method == "library.external.inspect") {
        const auto path = Path(String(params, "path"));
        const auto tree = host_.open_tree ? host_.open_tree(path) : nullptr;
        if (tree) {
            std::vector<std::pair<std::string, std::uint64_t>> files;
            for (const auto& file : tree->Files()) files.emplace_back(file.path, file.size);
            writer.AddString(result, "path", host_.label ? host_.label(path) : Text(path));
            DescribeFiles(writer, result, files);
        } else {
            DescribeDataDirectory(writer, result, path);
        }
        const auto shown = host_.label ? host_.label(path) : std::string{};
        const auto guest = app::GuessDataMount(shown.empty() ? path : Path(shown),
                                               params.Member("package") ? String(params, "package") : std::string{});
        if (guest)
            writer.AddString(result, "suggested_guest_dir", *guest);
        else
            writer.AddNull(result, "suggested_guest_dir");
    } else {
        if (!host_.analyze || !host_.timestamp) throw std::runtime_error("导入服务不可用。");
        if (method == "library.analyze") {
            // An APK, or a game archive (ZIP/RAR) holding one: told apart by content, not by name.
            const auto source = Path(String(params, "path"));
            std::optional<std::string> inner;
            if (params.Member("apk")) inner = String(params, "apk");
            job.Reserve(store_.Root());
            const auto snapshot = job.directory / "source.apk";
            job.analyzing = std::async(std::launch::async, [snapshot, source, inner, analyze = host_.analyze] {
                Analyzed result;
                const auto kind = DetectImportSource(source);
                if (kind == ImportSourceKind::unknown)
                    throw std::invalid_argument("请选择 APK，或内含 APK 的 ZIP/RAR 压缩包。");
                if (kind == ImportSourceKind::apk) {
                    if (inner) throw std::invalid_argument("apk applies to an archive");
                    const auto size = ImportFileSize(source);
                    if (size == 0 || size > maximum_bytes) throw std::invalid_argument("APK 必须非空且不超过 1 GB。");
                    CopyImportFile(source, snapshot);
                    result.analysis = analyze(snapshot);
                    return result;
                }
                std::shared_ptr<ImportSource> archive = OpenArchive(source);
                const auto& files = archive->Files();
                const auto first = PlanArchive(files, {});
                if (first.apks.empty()) throw std::invalid_argument("压缩包里没有找到 APK。");
                result.apk = first.apks.front();
                if (inner) {
                    const auto found = std::find_if(first.apks.begin(), first.apks.end(),
                                                    [&](std::size_t index) { return files[index].path == *inner; });
                    if (found == first.apks.end()) throw std::invalid_argument("压缩包里没有所选的 APK。");
                    result.apk = *found;
                }
                const ImportExtraction extraction{result.apk, snapshot};
                archive->Extract(std::span(&extraction, 1), nullptr);
                result.analysis = analyze(snapshot);
                result.plan = PlanArchive(files, result.analysis.manifest.package);
                result.archive = std::move(archive);
                return result;
            });
            job.state = "analyzing";
        } else if (method == "library.upload.begin") {
            const auto source = String(params, "name");
            ApkName(source);
            const auto size = Number(params, "size");
            if (size == 0 || size > maximum_bytes) throw std::invalid_argument("APK 必须非空且不超过 1 GB。");
            job.Reserve(store_.Root());
            const auto snapshot = job.directory / "source.apk";
            {
                std::ofstream stream(snapshot, std::ios::binary);
                if (!stream) throw std::runtime_error("无法创建导入临时文件。");
                job.expected = size;
                job.state = "uploading";
            }
        } else {
            if (String(params, "job") != job.job || job.job.empty())
                throw std::invalid_argument("unknown or expired job");
            job.Update();
            if (method == "library.job.cancel") {
                if (job.state == "imported") throw std::runtime_error("已加入游戏库，不能取消。");
                // Copying data honors the request; a finished APK-only commit may still win.
                if (job.committing.valid() && job.progress) job.progress->cancel_requested = true;
                job.cancelled = true;
                job.Update();
            } else if (method == "library.upload.chunk") {
                if (job.state != "uploading") throw std::runtime_error("任务不接受文件分块。");
                const auto offset = Number(params, "offset");
                const auto data = String(params, "data");
                if (data.size() > 262144 || offset != job.received)
                    throw std::invalid_argument("invalid upload chunk size or offset");
                const auto bytes = app::DecodeBase64(data, {.ignore_ascii_whitespace = false});
                if (!bytes || bytes->empty() || bytes->size() > job.expected - job.received)
                    throw std::invalid_argument("invalid upload bytes");
                std::ofstream stream(job.directory / "source.apk", std::ios::binary | std::ios::app);
                stream.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
                stream.close();
                if (!stream) {
                    job.state = "failed";
                    job.Clean();
                    throw std::runtime_error("写入安装包失败，请重新选择。");
                }
                job.received += bytes->size();
            } else if (method == "library.upload.finish") {
                if (job.state != "uploading" || job.received != job.expected)
                    throw std::invalid_argument("incomplete upload");
                const auto snapshot = job.directory / "source.apk";
                job.analyzing = std::async(std::launch::async, [snapshot, analyze = host_.analyze] {
                    Analyzed result;
                    result.analysis = analyze(snapshot);
                    return result;
                });
                job.state = "analyzing";
            } else if (method == "library.import") {
                if (job.state != "ready" || !job.analysis) throw std::runtime_error("分析尚未完成或任务已提交。");
                const auto confirmation = params.Member("new_instance");
                if (!confirmation || confirmation->Bool() != true)
                    throw std::invalid_argument("请确认另外添加一份，不覆盖现有游戏。");
                std::vector<std::string> extras;
                if (const auto list = params.Member("extras")) {
                    if (!list->IsArray()) throw std::invalid_argument("extras requires an array of names");
                    for (std::size_t index = 0; index < list->Size(); ++index) {
                        const auto name = list->Element(index)->String();
                        if (!name || name->empty()) throw std::invalid_argument("extras requires an array of names");
                        extras.emplace_back(*name);
                    }
                    if (!job.archive && !extras.empty()) throw std::invalid_argument("extras apply to an archive");
                }
                std::optional<std::filesystem::path> external;
                if (params.Member("external_dir")) external = Path(String(params, "external_dir"));
                const bool archive_data = job.archive && !ArchiveMount(job.plan, extras).empty();
                if (archive_data && external)
                    throw std::invalid_argument("压缩包里已有数据包，不能另选数据包目录。");
                auto mode = ExternalDataMode::copy;
                if (params.Member("external_mode")) {
                    const auto text = String(params, "external_mode");
                    if (text != "copy" && text != "link")
                        throw std::invalid_argument("external_mode must be copy or link");
                    mode = text == "link" ? ExternalDataMode::link : ExternalDataMode::copy;
                    if (mode == ExternalDataMode::link && !platform_.path_settings)
                        throw std::invalid_argument("external_mode must be copy on this platform");
                }
                std::optional<std::string> guest;
                if (params.Member("external_guest_dir")) guest = String(params, "external_guest_dir");
                // Archive data and a host directory handle are written into the job directory first, then moved in.
                std::shared_ptr<ImportSource> tree;
                if (external && host_.open_tree) tree = host_.open_tree(*external);
                if (tree && mode == ExternalDataMode::link)
                    throw std::invalid_argument("external_mode must be copy for this directory");
                // A plain request is checked here, so a bad directory or mount leaves the analysis ready to retry.
                std::optional<LibraryImport> ready;
                if (!archive_data && !tree)
                    ready = BuildLibraryImport(*job.analysis, external, host_.timestamp(), guest.value_or("/sdcard"), mode);
                else if (guest)
                    static_cast<void>(BuildLibraryImport(*job.analysis, std::nullopt, host_.timestamp(), *guest));
                auto store = store_;
                job.progress = std::make_shared<ImportProgress>();
                job.committing = std::async(std::launch::async, [store, ready, analysis = *job.analysis, external, mode, guest,
                                                                 archive = archive_data ? job.archive : nullptr,
                                                                 plan = job.plan, extras, tree,
                                                                 data = job.directory / "data", timestamp = host_.timestamp(),
                                                                 progress = job.progress]() mutable {
                    if (ready) return store.Import(*ready, true, progress.get());
                    auto source = external;
                    auto chosen_mode = mode;
                    auto mount = guest.value_or("/sdcard");
                    if (archive) {
                        const auto planned = ExtractArchiveData(*archive, plan, extras, data, progress.get());
                        if (!guest) mount = planned;
                        source = data;
                        chosen_mode = ExternalDataMode::move;
                    } else if (tree) {
                        std::vector<ImportExtraction> work;
                        std::uint64_t total{};
                        for (std::size_t index = 0; index < tree->Files().size(); ++index) {
                            work.push_back({index, data / ImportFilePath(tree->Files()[index].path)});
                            total += tree->Files()[index].size;
                        }
                        std::filesystem::create_directories(data);
                        progress->total_bytes = total;
                        tree->Extract(work, progress.get());
                        source = data;
                        chosen_mode = ExternalDataMode::move;
                    }
                    const auto request = BuildLibraryImport(analysis, source, timestamp, mount, chosen_mode);
                    return store.Import(request, true, progress.get());
                });
                job.state = "importing";
            }
        }
        if (job.state == "failed" &&
            (job.failure_code == LibraryErrorCode::busy || job.failure_code == LibraryErrorCode::running))
            throw LibraryError(*job.failure_code, job.failure);
        writer.AddString(result, "job", job.job);
        // A commit stays "importing" until the worker has actually stopped.
        writer.AddString(result, "state", job.cancelled && !job.committing.valid() ? "cancelled" : job.state);
        if (job.state == "failed") writer.AddString(result, "message", job.failure);
        if (job.state == "imported") writer.AddString(result, "installation_id", job.installed);
        if (job.state == "importing" && job.progress) {
            const auto progress = writer.Object();
            writer.AddUnsignedInteger(progress, "copied_bytes",
                                      job.progress->copied_bytes.load(std::memory_order_relaxed));
            writer.AddUnsignedInteger(progress, "total_bytes",
                                      job.progress->total_bytes.load(std::memory_order_relaxed));
            writer.Add(result, "progress", progress);
        }
        if (job.state == "ready" && job.analysis && !job.cancelled) {
            const auto& analysis = *job.analysis;
            const auto summary = writer.Object();
            writer.AddString(summary, "display_name", analysis.display_name);
            const auto names = writer.Object();
            for (const auto& [locale, name] : analysis.localized_names)
                writer.AddString(names, locale, name);
            writer.Add(summary, "localized_names", names);
            writer.AddString(summary, "package", analysis.manifest.package);
            writer.AddString(summary, "version_name", analysis.manifest.version_name.value_or(""));
            writer.AddUnsignedInteger(summary, "version_code", analysis.manifest.version_code);
            if (analysis.manifest.min_sdk)
                writer.AddUnsignedInteger(summary, "min_sdk", *analysis.manifest.min_sdk);
            else
                writer.AddNull(summary, "min_sdk");
            if (analysis.manifest.target_sdk)
                writer.AddUnsignedInteger(summary, "target_sdk", *analysis.manifest.target_sdk);
            else
                writer.AddNull(summary, "target_sdk");
            const auto abis = writer.Array();
            for (const auto& abi : analysis.abis)
                writer.Append(abis, writer.String(abi));
            writer.Add(summary, "abis", abis);
            writer.AddString(
                summary, "icon",
                analysis.icon_png.empty() ? "" : "data:image/png;base64," + app::EncodeBase64(analysis.icon_png));
            if (analysis.profile)
                writer.AddString(summary, "profile", analysis.profile->profile_id);
            else
                writer.AddNull(summary, "profile");
            const auto entries = store_.LoadEntries();
            writer.AddUnsignedInteger(
                summary, "existing_instances",
                static_cast<std::uint64_t>(std::count_if(entries.begin(), entries.end(), [&](const auto& entry) {
                    return entry.metadata && entry.metadata->package == analysis.manifest.package;
                })));
            if (job.archive) {
                const auto& files = job.archive->Files();
                const auto archive = writer.Object();
                const auto apks = writer.Array();
                for (const auto index : job.plan.apks) {
                    const auto item = writer.Object();
                    writer.AddString(item, "path", files[index].path);
                    writer.AddUnsignedInteger(item, "size", files[index].size);
                    writer.Append(apks, item);
                }
                writer.Add(archive, "apks", apks);
                writer.AddString(archive, "apk", files[job.apk].path);
                const auto mount = ArchiveMount(job.plan, {});
                if (mount.empty()) {
                    writer.AddNull(archive, "data");
                } else {
                    const auto data = writer.Object();
                    writer.AddString(data, "guest_dir", mount);
                    writer.AddString(data, "path", mount);
                    DescribeFiles(writer, data, Under(*job.archive, job.plan, mount));
                    // With extras chosen everything mounts at /sdcard: what then shows at its top level.
                    const auto root = writer.Object();
                    DescribeFiles(writer, root, Under(*job.archive, job.plan, "/sdcard"));
                    writer.Add(data, "sdcard", root);
                    writer.Add(archive, "data", data);
                }
                const auto extras = writer.Array();
                for (const auto& extra : job.plan.extras) {
                    const auto item = writer.Object();
                    writer.AddString(item, "name", extra.name);
                    writer.AddUnsignedInteger(item, "files", extra.members.size());
                    writer.AddUnsignedInteger(item, "bytes", extra.bytes);
                    writer.Append(extras, item);
                }
                writer.Add(archive, "extras", extras);
                writer.Add(summary, "archive", archive);
            } else {
                writer.AddNull(summary, "archive");
            }
            writer.Add(result, "summary", summary);
        }
    }
    writer.Add(root, "result", result);
    return {true, writer.Serialize(root)};
}
}  // namespace ogplay::frontend
