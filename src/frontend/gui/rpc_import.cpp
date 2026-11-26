#include "frontend/gui/rpc.h"
#include "frontend/gui/rpc_error.h"
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
    if (!name.ends_with(".apk")) throw GuiArgumentError({"host.error.splitApk"});
}

// Summarizes a candidate data directory for the mount preview: a few top-level
// names plus bounded totals, so huge trees cannot stall the RPC thread.
void DescribeDataDirectory(app::JsonWriter& writer, app::JsonWriter::Value result, const std::filesystem::path& path) {
    constexpr std::size_t shown_entries = 8, scan_limit = 200000;
    std::error_code error;
    if (!std::filesystem::is_directory(path, error) || error) throw GuiArgumentError({"host.error.dataUnavailable"});
    std::vector<std::pair<bool, std::string>> top;
    for (std::filesystem::directory_iterator it(path, error), end; !error && it != end; it.increment(error))
        top.emplace_back(!it->is_directory(error), Text(it->path().filename()));
    if (error) throw GuiTextError({"host.error.dataUnreadable"});
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
    if (error) throw GuiTextError({"host.error.dataUnmeasurable"});
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

// A planned file's entry in the mount preview: its top-level name below mount, or, when mount lies above an app's
// own directory (/sdcard/Android/<kind>/<package>), the path down to that directory so the package shows.
std::pair<std::string, bool> PreviewEntry(std::string_view mount, const std::string& path) {
    const auto slash = path.find('/');
    std::pair<std::string, bool> entry{path.substr(0, slash), slash != std::string::npos};
    if (mount.empty()) return entry;
    const auto guest = std::string(mount) + "/" + path;
    constexpr std::string_view android = "/sdcard/Android/";
    if (!guest.starts_with(android)) return entry;
    const auto kind_end = guest.find('/', android.size());
    const auto kind = kind_end == std::string::npos
                          ? std::string_view{}
                          : std::string_view(guest).substr(android.size(), kind_end - android.size());
    if (kind != "data" && kind != "obb" && kind != "media") return entry;
    const auto package_end = guest.find('/', kind_end + 1);
    if (package_end == std::string::npos || package_end <= mount.size()) return entry;
    return {guest.substr(mount.size() + 1, package_end - mount.size() - 1), true};
}

// The same summary as DescribeDataDirectory for files that are not on disk yet (an archive's data, a SAF
// directory): top-level names below mount (see PreviewEntry; "" for a plain listing) and totals.
void DescribeFiles(app::JsonWriter& writer, app::JsonWriter::Value result,
                   const std::vector<std::pair<std::string, std::uint64_t>>& files, std::string_view mount = {}) {
    constexpr std::size_t shown_entries = 8;
    std::map<std::string, bool> top;  // name -> directory
    std::uint64_t bytes{};
    for (const auto& [path, size] : files) {
        const auto [name, is_directory] = PreviewEntry(mount, path);
        auto& directory = top[name];
        directory = directory || is_directory;
        bytes += size;
    }
    std::vector<std::pair<bool, std::string>> sorted;
    for (const auto& [name, directory] : top)
        sorted.emplace_back(!directory, name);
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
        if (item.guest_path.size() > mount.size() && item.guest_path.starts_with(mount) &&
            item.guest_path[mount.size()] == '/')
            files.emplace_back(item.guest_path.substr(mount.size() + 1), source.Files()[item.index].size);
    return files;
}

// An archive's recognized data (with its planned mount) and its parts of no known use, as the wizard shows them.
void DescribeArchiveData(app::JsonWriter& writer, app::JsonWriter::Value archive, const ImportSource& source,
                         const ArchivePlan& plan) {
    const auto mount = ArchiveMount(plan, {});
    if (mount.empty()) {
        writer.AddNull(archive, "data");
    } else {
        const auto data = writer.Object();
        writer.AddString(data, "guest_dir", mount);
        writer.AddString(data, "path", mount);
        DescribeFiles(writer, data, Under(source, plan, mount), mount);
        // With extras chosen everything mounts at /sdcard: what then shows at its top level.
        const auto root = writer.Object();
        DescribeFiles(writer, root, Under(source, plan, "/sdcard"), "/sdcard");
        writer.Add(data, "sdcard", root);
        writer.Add(archive, "data", data);
    }
    const auto extras = writer.Array();
    for (const auto& extra : plan.extras) {
        const auto item = writer.Object();
        writer.AddString(item, "name", extra.name);
        writer.AddUnsignedInteger(item, "files", extra.members.size());
        writer.AddUnsignedInteger(item, "bytes", extra.bytes);
        writer.AddBool(item, "directory", extra.directory);
        writer.Append(extras, item);
    }
    writer.Add(archive, "extras", extras);
}

// The host directory a linked data pack refers to and its mount: the directory standing for the planned mount,
// inside the picked one or an ancestor named as the mount's parts below it (an app's own directory above a
// picked files/). An ancestor that does not stand for the mount (the folder a Gameloft game's directory was
// picked from) is never linked: the picked directory is then linked at its own place.
std::pair<std::filesystem::path, std::string> LinkedDataDirectory(const std::filesystem::path& picked,
                                                                  std::string_view base,
                                                                  const std::vector<ImportFile>& files,
                                                                  const ArchivePlan& plan) {
    const auto mount = ArchiveMount(plan, {});
    auto linked = std::filesystem::absolute(picked).lexically_normal();
    if (linked.filename().empty()) linked = linked.parent_path();
    const std::pair<std::filesystem::path, std::string> own{linked, std::string(base)};
    const auto fold = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const auto parts = [](std::string_view text) {
        std::vector<std::string> result;
        for (std::size_t start = 0; start <= text.size();) {
            const auto end = std::min(text.find('/', start), text.size());
            result.emplace_back(text.substr(start, end - start));
            start = end + 1;
        }
        return result;
    };
    const auto& item = plan.data.front();
    const auto host = parts(files[item.index].path);
    const auto below = parts(std::string_view(item.guest_path).substr(mount.size() + 1));
    const auto common = std::min(host.size(), below.size());
    for (std::size_t step = 1; step <= common; ++step)
        if (fold(host[host.size() - step]) != fold(below[below.size() - step])) return own;
    if (below.size() <= host.size()) {
        std::string inside;
        for (std::size_t index = 0; index < host.size() - below.size(); ++index)
            inside.append(index ? "/" : "").append(host[index]);
        return {inside.empty() ? linked : linked / ImportFilePath(inside), mount};
    }
    for (auto index = below.size() - host.size(); index-- > 0;) {
        if (fold(Text(linked.filename())) != fold(below[index])) return own;
        linked = linked.parent_path();
    }
    const auto name = mount.substr(mount.rfind('/') + 1);
    if (fold(Text(linked.filename())) != fold(name)) return own;
    return {linked, mount};
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
    std::optional<app::UiMessage> failure_ui;
    std::unique_ptr<ImportWorkDirectory> work;  // the job's snapshot and extracted data, swept if the process dies
    std::filesystem::path directory;
    std::uint64_t expected{}, received{};
    std::optional<ApkImportAnalysis> analysis;
    std::shared_ptr<ImportSource> archive;
    ArchivePlan plan;
    std::size_t apk{};
    // A data pack picked apart from the APK (directory, host directory handle or archive) and its plan; it stands
    // in for the APK archive's data when set. A host directory keeps its path and guest place for linking.
    std::shared_ptr<ImportSource> data_source;
    ArchivePlan data_plan;
    std::optional<std::filesystem::path> data_directory;
    std::string data_base;
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
        data_source.reset();
        data_plan = {};
        data_directory.reset();
        data_base.clear();
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
            if (const auto* ui = app::FindUiMessage(error)) failure_ui = *ui;
            state = "failed";
            Clean();
        } catch (const std::exception& error) {
            if (cancelled) {
                Clean();
                state = "cancelled";
                return;
            }
            failure = error.what();
            if (const auto* ui = app::FindUiMessage(error)) failure_ui = *ui;
            state = "failed";
            Clean();
        }
    }

    void Reserve(const std::filesystem::path& root) {
        Update();
        if (analyzing.valid() || committing.valid() || state == "ready" || state == "uploading")
            throw GuiTextError({"host.error.importInProgress"});
        Clean();
        work = std::make_unique<ImportWorkDirectory>(root);
        directory = work->Path();
        job = std::to_string(++serial);
        failure_code.reset();
        failure_ui.reset();
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
    else if (method == "library.data_pack")
        Fields(params, {"job", "path"});
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
        if (!host_.pick) throw GuiTextError({"host.error.noPicker"});
        if (job.dialog_pending) throw GuiTextError({"host.error.pickerOpen"});
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
            for (const auto& file : tree->Files())
                files.emplace_back(file.path, file.size);
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
    } else if (method == "library.data_pack") {
        // A data pack picked apart from the APK, planned for the analyzed package; no path detaches it.
        if (String(params, "job") != job.job || job.job.empty()) throw std::invalid_argument("unknown or expired job");
        job.Update();
        if (job.state != "ready" || !job.analysis || job.cancelled) throw GuiTextError({"host.error.notReady"});
        job.data_source.reset();
        job.data_plan = {};
        job.data_directory.reset();
        job.data_base.clear();
        if (params.Member("path")) {
            const auto path = Path(String(params, "path"));
            const auto& package = job.analysis->manifest.package;
            std::shared_ptr<ImportSource> source = host_.open_tree ? host_.open_tree(path) : nullptr;
            const bool handle = source != nullptr;
            std::error_code error;
            const bool directory = source || std::filesystem::is_directory(path, error);
            ArchivePlan plan;
            std::optional<std::string> owner;
            std::string base;
            if (directory) {
                // Directories go by the package's own directories inside them, placed by where the directory sits.
                const auto shown = host_.label ? host_.label(path) : std::string{};
                base = app::GuessDataMount(shown.empty() ? path : Path(shown), package).value_or("/sdcard");
                if (!source) source = OpenDataDirectory(path, store_.Root());
                auto planned = PlanDataDirectory(source->Files(), base, package);
                if (planned.plan.data.empty() && planned.plan.extras.empty())
                    throw GuiArgumentError({"host.error.dataEmpty"});
                plan = std::move(planned.plan);
                if (!planned.package.empty()) owner = std::move(planned.package);
            } else {
                const auto kind = DetectImportSource(path);
                if (kind == ImportSourceKind::apk) throw GuiArgumentError({"host.error.isApk"});
                if (kind == ImportSourceKind::unknown) throw GuiArgumentError({"host.error.chooseDataArchive"});
                source = OpenArchive(path);
                plan = PlanArchive(source->Files(), package);
                if (plan.data.empty() && plan.extras.empty()) throw GuiArgumentError({"host.error.archiveNothing"});
            }
            writer.AddString(result, "kind", directory ? "directory" : "archive");
            if (owner)
                writer.AddString(result, "package", *owner);
            else
                writer.AddNull(result, "package");
            DescribeArchiveData(writer, result, *source, plan);
            job.data_source = std::move(source);
            job.data_plan = std::move(plan);
            if (directory && !handle) job.data_directory = path;
            job.data_base = std::move(base);
        }
    } else {
        if (!host_.analyze || !host_.timestamp) throw GuiTextError({"host.error.noImportService"});
        if (method == "library.analyze") {
            // An APK, or a game archive (ZIP/RAR/7z) holding one: told apart by content, not by name.
            const auto source = Path(String(params, "path"));
            std::optional<std::string> inner;
            if (params.Member("apk")) inner = String(params, "apk");
            job.Reserve(store_.Root());
            const auto snapshot = job.directory / "source.apk";
            job.analyzing = std::async(std::launch::async, [snapshot, source, inner, analyze = host_.analyze] {
                Analyzed result;
                const auto kind = DetectImportSource(source);
                if (kind == ImportSourceKind::unknown) throw GuiArgumentError({"host.error.chooseApkOrArchive"});
                if (kind == ImportSourceKind::apk) {
                    if (inner) throw std::invalid_argument("apk applies to an archive");
                    const auto size = ImportFileSize(source);
                    if (size == 0 || size > maximum_bytes) throw GuiArgumentError({"host.error.apkSize"});
                    CopyImportFile(source, snapshot);
                    result.analysis = analyze(snapshot);
                    return result;
                }
                std::shared_ptr<ImportSource> archive = OpenArchive(source);
                const auto& files = archive->Files();
                const auto first = PlanArchive(files, {});
                if (first.apks.empty()) throw GuiArgumentError({"host.error.archiveNoApk"});
                result.apk = first.apks.front();
                if (inner) {
                    const auto found = std::find_if(first.apks.begin(), first.apks.end(),
                                                    [&](std::size_t index) { return files[index].path == *inner; });
                    if (found == first.apks.end()) throw GuiArgumentError({"host.error.archiveApkMissing"});
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
            if (size == 0 || size > maximum_bytes) throw GuiArgumentError({"host.error.apkSize"});
            job.Reserve(store_.Root());
            const auto snapshot = job.directory / "source.apk";
            {
                std::ofstream stream(snapshot, std::ios::binary);
                if (!stream) throw GuiTextError({"host.error.tempFile"});
                job.expected = size;
                job.state = "uploading";
            }
        } else {
            if (String(params, "job") != job.job || job.job.empty())
                throw std::invalid_argument("unknown or expired job");
            job.Update();
            if (method == "library.job.cancel") {
                if (job.state == "imported") throw GuiTextError({"host.error.alreadyAdded"});
                // Copying data honors the request; a finished APK-only commit may still win.
                if (job.committing.valid() && job.progress) job.progress->cancel_requested = true;
                job.cancelled = true;
                job.Update();
            } else if (method == "library.upload.chunk") {
                if (job.state != "uploading") throw GuiTextError({"host.error.noChunks"});
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
                    throw GuiTextError({"host.error.writePackage"});
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
                if (job.state != "ready" || !job.analysis) throw GuiTextError({"host.error.notReady"});
                const auto confirmation = params.Member("new_instance");
                if (!confirmation || confirmation->Bool() != true)
                    throw GuiArgumentError({"host.error.confirmDuplicate"});
                std::vector<std::string> extras;
                if (const auto list = params.Member("extras")) {
                    if (!list->IsArray()) throw std::invalid_argument("extras requires an array of names");
                    for (std::size_t index = 0; index < list->Size(); ++index) {
                        const auto name = list->Element(index)->String();
                        if (!name || name->empty()) throw std::invalid_argument("extras requires an array of names");
                        extras.emplace_back(*name);
                    }
                }
                // Data comes out of the separately picked data pack, else out of the APK's own archive.
                const auto& data_source = job.data_source ? job.data_source : job.archive;
                const auto& data_plan = job.data_source ? job.data_plan : job.plan;
                if (!data_source && !extras.empty()) throw std::invalid_argument("extras apply to an archive");
                std::optional<std::filesystem::path> external;
                if (params.Member("external_dir")) external = Path(String(params, "external_dir"));
                bool archive_data = data_source && !ArchiveMount(data_plan, extras).empty();
                if (job.data_source && !archive_data) throw GuiArgumentError({"host.error.nothingSelected"});
                if (archive_data && external) throw GuiArgumentError({"host.error.archiveHasData"});
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
                if (mode == ExternalDataMode::link && job.data_source) {
                    // Linking a picked directory refers to the host directory at the planned mount, extras aside.
                    if (!job.data_directory)
                        throw std::invalid_argument("external_mode must be copy for this data pack");
                    if (!extras.empty()) throw GuiArgumentError({"host.error.linkNoExtras"});
                    const auto [linked, mount] = LinkedDataDirectory(*job.data_directory, job.data_base,
                                                                     job.data_source->Files(), job.data_plan);
                    external = linked;
                    if (!guest) guest = mount;
                    archive_data = false;
                }
                // Archive data and a host directory handle are written into the job directory first, then moved in.
                std::shared_ptr<ImportSource> tree;
                if (external && host_.open_tree) tree = host_.open_tree(*external);
                if (tree && mode == ExternalDataMode::link)
                    throw std::invalid_argument("external_mode must be copy for this directory");
                // A plain request is checked here, so a bad directory or mount leaves the analysis ready to retry.
                std::optional<LibraryImport> ready;
                if (!archive_data && !tree)
                    ready =
                        BuildLibraryImport(*job.analysis, external, host_.timestamp(), guest.value_or("/sdcard"), mode);
                else if (guest)
                    static_cast<void>(BuildLibraryImport(*job.analysis, std::nullopt, host_.timestamp(), *guest));
                auto store = store_;
                job.progress = std::make_shared<ImportProgress>();
                job.committing =
                    std::async(std::launch::async, [store, ready, analysis = *job.analysis, external, mode, guest,
                                                    archive = archive_data ? data_source : nullptr, plan = data_plan,
                                                    extras, tree, data = job.directory / "data",
                                                    timestamp = host_.timestamp(), progress = job.progress]() mutable {
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
            throw LibraryError(*job.failure_code, job.failure, job.failure_ui.value_or(app::UiMessage{}));
        writer.AddString(result, "job", job.job);
        // A commit stays "importing" until the worker has actually stopped.
        writer.AddString(result, "state", job.cancelled && !job.committing.valid() ? "cancelled" : job.state);
        if (job.state == "failed")
            writer.Add(result, "failure", UiTextJson(writer, job.failure, job.failure_ui ? &*job.failure_ui : nullptr));
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
                DescribeArchiveData(writer, archive, *job.archive, job.plan);
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
