// Guest filesystem assembly: Android standard paths and per-installation saves.

#include "run_game_vfs.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/core/encoding.h"
#include "ogplay/core/json.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/hal/host_environment.h"

namespace ogplay::frontend {
namespace {

std::string PathUtf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};
[[nodiscard]] std::array<std::byte, 8> AndroidIdEntropy() {
    std::array<std::byte, 8> entropy{};
    hal::FillSecureRandom(entropy);
    return entropy;
}

[[nodiscard]] bool IsInstallationIdForPackage(
    const std::string_view installation_id, const std::string_view package) {
    if (installation_id == package) return true;
    if (!installation_id.starts_with(package) ||
        installation_id.size() <= package.size() + 1U ||
        installation_id[package.size()] != '-') {
        return false;
    }
    const auto suffix = installation_id.substr(package.size() + 1U);
    if (suffix.front() == '0') return false;
    std::uint32_t number{};
    const auto parsed = std::from_chars(
        suffix.data(), suffix.data() + suffix.size(), number);
    return parsed.ec == std::errc{} &&
           parsed.ptr == suffix.data() + suffix.size() && number >= 2U;
}

[[nodiscard]] std::string ResolveInstallationId(
    const std::filesystem::path& root, const std::string_view package) {
    std::vector<std::string> matches;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end; iterator.increment(error)) {
        if (!iterator->is_directory(error)) continue;
        const auto name = iterator->path().filename().string();
        if (IsInstallationIdForPackage(name, package)) {
            matches.push_back(name);
        }
    }
    if (error) {
        throw std::runtime_error(
            "cannot inspect save sandbox installation ids at " +
            PathUtf8(root));
    }
    if (matches.empty()) return std::string(package);
    if (matches.size() == 1U) return matches.front();
    std::sort(matches.begin(), matches.end());
    throw std::runtime_error(
        "multiple save sandbox installations exist for package " +
        std::string(package) + "; pass --installation-id <id>");
}

}  // namespace

std::string SandboxSession::Describe() const {
    if (!store) return "ephemeral";
    return PathUtf8(root) + " used=" + std::to_string(store->UsedBytes()) +
           "/" + std::to_string(store->QuotaBytes());
}

struct MissingDataRecorder::State final {
    static constexpr std::size_t kLimit = 512;
    std::mutex mutex;
    std::vector<std::string> paths;  // first-seen order
    std::unordered_set<std::string> seen;
    bool truncated{};
    void Record(const std::string_view path) {
        if (path != "/sdcard" && !path.starts_with("/sdcard/")) return;
        std::scoped_lock lock(mutex);
        if (seen.contains(std::string(path))) return;
        if (paths.size() == kLimit) { truncated = true; return; }
        seen.emplace(path);
        paths.emplace_back(path);
    }
};

MissingDataRecorder::MissingDataRecorder(runtime::VirtualFileSystem& filesystem, core::Logger& logger,
                                         std::filesystem::path report,
                                         std::optional<std::filesystem::path> external_directory,
                                         std::string external_guest_directory)
    : filesystem_(filesystem), logger_(logger), state_(std::make_shared<State>()), report_(std::move(report)),
      external_directory_(std::move(external_directory)), external_guest_directory_(std::move(external_guest_directory)) {
    // A stale report from an earlier run must never describe this one.
    std::error_code error;
    static_cast<void>(std::filesystem::remove(report_, error));
    filesystem_.SetMissingPathObserver([state = state_](const std::string_view path) { state->Record(path); });
}

MissingDataRecorder::~MissingDataRecorder() {
    Write();
}

void MissingDataRecorder::Write() noexcept {
    if (written_) return;
    written_ = true;
    try {
        // Detach first: the existence checks below must not record themselves.
        filesystem_.SetMissingPathObserver({});
        std::vector<std::string> missing;
        bool truncated{};
        {
            std::scoped_lock lock(state_->mutex);
            missing = state_->paths;
            truncated = state_->truncated;
        }
        std::erase_if(missing, [&](const std::string& path) {
            try { static_cast<void>(filesystem_.Stat(path)); return true; }
            catch (const runtime::VfsError&) { return false; }
        });
        core::JsonWriter writer;
        const auto root = writer.Object();
        writer.AddUnsignedInteger(root, "schema", 1);
        if (external_directory_) writer.AddString(root, "external_dir", PathUtf8(*external_directory_));
        else writer.AddNull(root, "external_dir");
        writer.AddString(root, "external_guest_dir", external_guest_directory_);
        const auto paths = writer.Array();
        for (const auto& path : missing) writer.Append(paths, writer.String(path));
        writer.Add(root, "missing", paths);
        writer.AddBool(root, "truncated", truncated);
        const auto text = writer.Serialize(root);
        if (!report_.parent_path().empty()) std::filesystem::create_directories(report_.parent_path());
        auto temporary = report_;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!output) throw std::runtime_error("cannot write missing data report");
        }
        std::filesystem::rename(temporary, report_);
        logger_.Write(core::LogLevel::info, "frontend.missing_data", "missing data report written", {},
                      {{"path", PathUtf8(report_)}, {"missing", static_cast<std::uint64_t>(missing.size())},
                       {"truncated", truncated}}, kUnrestrictedLog);
    } catch (const std::exception& error) {
        // A diagnostic report never changes how the game session ends.
        try {
            logger_.Write(core::LogLevel::warn, "frontend.missing_data", error.what(), {},
                          {{"path", PathUtf8(report_)}}, kUnrestrictedLog);
        } catch (...) {}
    }
}

void MountExternalDirectory(
    const std::optional<std::filesystem::path>& directory,
    runtime::VirtualFileSystem& filesystem,
    const std::optional<std::string>& guest_directory) {
    // Android primary-storage names must resolve to the same mounted nodes.
    // Canonicalizing the storage path preserves shared file and save state.
    filesystem.AddPathAlias("/storage/emulated/0", "/sdcard");
    filesystem.AddPathAlias("/mnt/sdcard", "/sdcard");
    if (!directory.has_value()) return;
    const auto guest = guest_directory.value_or("/sdcard");
    if (guest != "/sdcard" && !guest.starts_with("/sdcard/")) {
        throw std::invalid_argument("--external-guest-dir must be within /sdcard");
    }
    filesystem.MountHostDirectory(guest, *directory);
}

SandboxSession OpenSandbox(const SandboxOptions& options,
                           const std::string& package,
                           const std::uint32_t version_code) {
    SandboxSession session;
    if (options.ephemeral) {
        session.android_id = core::EncodeHex(
            AndroidIdEntropy(), core::HexCase::lower);
        session.installation_id = "ephemeral-" + session.android_id;
        return session;
    }
    if (options.directory.has_value()) {
        session.root = *options.directory;
    } else {
        const auto resolved = DefaultSandboxRoot();
        if (!resolved.has_value()) {
            throw std::runtime_error(
                "cannot resolve the user data directory for saves; pass "
                "--sandbox-dir <dir> or --ephemeral-sandbox");
        }
        session.root = *resolved;
    }
    std::error_code root_error;
    std::filesystem::create_directories(session.root, root_error);
    if (root_error) {
        throw std::runtime_error(
            "cannot create the save sandbox root at " + PathUtf8(session.root));
    }
    try {
        if (options.installation_id.has_value() &&
            options.installation_id->empty()) {
            throw std::runtime_error("--installation-id requires a non-empty id");
        }
        session.installation_id = options.installation_id.has_value()
                                      ? *options.installation_id
                                      : ResolveInstallationId(session.root, package);
        session.store = runtime::SandboxStore::Open(
            session.root, session.installation_id, package);
        session.android_id = session.store->AndroidId().value_or("");
        if (session.android_id.empty()) {
            session.android_id =
                session.store->EnsureAndroidId(AndroidIdEntropy());
        }
        session.store->RecordVersionCode(version_code);
    } catch (const runtime::VfsError& error) {
        // The repair action has to be in the message: the user owns this
        // directory and may well have edited it.
        throw std::runtime_error(
            "cannot open the save sandbox at " + PathUtf8(session.root) + ": " +
            error.what() +
            " (back up and clear that directory, or pass "
            "--ephemeral-sandbox)");
    }
    return session;
}

void AttachSandbox(const SandboxSession& sandbox,
                   const std::string& package,
                   runtime::VirtualFileSystem& filesystem,
                   core::Logger& logger) {
    const auto writable_roots = std::vector<std::string>{"/data/data/" + package, "/sdcard"};
    if (!sandbox.store) {
        // The writable roots are directories on the platform whether or not
        // saves persist, so an ephemeral run behaves the same up to the
        // persistence itself.
        for (const auto& root : writable_roots) {
            try {
                filesystem.CreateDirectory(root);
            } catch (const runtime::VfsError&) {
                // Already present through a mounted base layer.
            }
        }
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "ephemeral sandbox: saves are discarded at exit", {}, {},
                     kUnrestrictedLog);
        return;
    }
    filesystem.AttachSandbox(*sandbox.store, writable_roots);
    for (const auto& root : sandbox.store->RecreatedSemanticRoots()) {
        logger.Write(core::LogLevel::warn, "frontend.run_apk",
                     "recreated missing save sandbox semantic root", {},
                     {{"installation_id", sandbox.store->InstallationId()},
                      {"root", PathUtf8(sandbox.store->Directory() / root)}},
                     kUnrestrictedLog);
    }
    if (sandbox.store->TemporaryFilesRemoved() != 0) {
        logger.Write(core::LogLevel::warn, "frontend.run_apk",
                     "cleared save sandbox temporaries left by an earlier "
                     "crash", {},
                     {{"count", sandbox.store->TemporaryFilesRemoved()}},
                     kUnrestrictedLog);
    }
    logger.Write(core::LogLevel::info, "frontend.run_apk",
                 "save sandbox attached", {},
                 {{"root", PathUtf8(sandbox.root)},
                  {"package", sandbox.store->Package()},
                  {"used_bytes", sandbox.store->UsedBytes()},
                  {"quota_bytes", sandbox.store->QuotaBytes()}},
                 kUnrestrictedLog);
}

}  // namespace ogplay::frontend
