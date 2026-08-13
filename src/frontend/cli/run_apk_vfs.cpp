// Guest filesystem assembly for run-apk (ADR-0020): the Profile's read-only
// base layers first, then the per-title save sandbox on top.

#include "run_apk_vfs.h"

#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/session/profile_vfs.h"

namespace ogplay::frontend {
namespace {

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};

[[nodiscard]] const session::ProfileMount* ExternalMount(
    const session::TitleProfile& profile) {
    if (!profile.data.has_value()) return nullptr;
    const session::ProfileMount* result = nullptr;
    for (const auto& mount : profile.data->mounts) {
        if (mount.source != session::ProfileSource::external) continue;
        if (result != nullptr) {
            throw std::runtime_error(
                "run-apk supports one external Profile mount per session");
        }
        result = &mount;
    }
    return result;
}

[[nodiscard]] std::string JoinGuestPath(const std::string_view root,
                                        const std::string_view relative) {
    auto result = std::string(root);
    if (result != "/") result.push_back('/');
    result.append(relative);
    return result;
}

}  // namespace

std::string SandboxSession::Describe() const {
    if (!store) return "ephemeral";
    return root.string() + " used=" + std::to_string(store->UsedBytes()) +
           "/" + std::to_string(store->QuotaBytes());
}

void MountExternalDirectory(
    const session::TitleProfile& profile,
    const std::optional<std::filesystem::path>& directory,
    runtime::VirtualFileSystem& filesystem) {
    const auto* mount = ExternalMount(profile);
    if (mount == nullptr) {
        if (directory.has_value()) {
            throw std::runtime_error(
                "--external-dir was supplied but Profile declares no external mount");
        }
        return;
    }
    if (!directory.has_value()) {
        if (mount->required) {
            throw std::runtime_error(
                "Profile requires --external-dir for guest mount " +
                mount->guest);
        }
        return;
    }
    filesystem.MountHostDirectory(mount->guest, *directory);
    for (const auto& entry : profile.data->manifest) {
        if (!entry.required) continue;
        try {
            static_cast<void>(filesystem.Stat(
                JoinGuestPath(mount->guest, entry.path)));
        } catch (const runtime::VfsError& error) {
            if (error.ErrorNumber() != 2) throw;
            throw std::runtime_error(
                "required Profile manifest file is missing: " + entry.path);
        }
    }
}

SandboxSession OpenSandbox(const SandboxOptions& options,
                           const std::string& package,
                           const std::uint32_t version_code) {
    SandboxSession session;
    if (options.ephemeral) return session;
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
    try {
        session.store = runtime::SandboxStore::Open(session.root, package);
    } catch (const runtime::VfsError& error) {
        // The repair action has to be in the message: the user owns this
        // directory and may well have edited it.
        throw std::runtime_error(
            "cannot open the save sandbox at " + session.root.string() + ": " +
            error.what() +
            " (back up and clear that directory, or pass "
            "--ephemeral-sandbox)");
    }
    session.store->RecordVersionCode(version_code);
    return session;
}

void AttachSandbox(const SandboxSession& sandbox,
                   const session::TitleProfile& profile,
                   runtime::VirtualFileSystem& filesystem,
                   core::Logger& logger) {
    if (!sandbox.store) {
        logger.Write(core::LogLevel::info, "frontend.run_apk",
                     "ephemeral sandbox: saves are discarded at exit", {}, {},
                     kUnrestrictedLog);
        return;
    }
    const auto writable_roots = session::ProfileWritableRoots(profile);
    filesystem.AttachSandbox(*sandbox.store, writable_roots);
    if (sandbox.store->TemporaryFilesRemoved() != 0) {
        logger.Write(core::LogLevel::warn, "frontend.run_apk",
                     "cleared save sandbox temporaries left by an earlier "
                     "crash", {},
                     {{"count", sandbox.store->TemporaryFilesRemoved()}},
                     kUnrestrictedLog);
    }
    logger.Write(core::LogLevel::info, "frontend.run_apk",
                 "save sandbox attached", {},
                 {{"root", sandbox.root.string()},
                  {"package", sandbox.store->Package()},
                  {"used_bytes", sandbox.store->UsedBytes()},
                  {"quota_bytes", sandbox.store->QuotaBytes()}},
                 kUnrestrictedLog);
}

}  // namespace ogplay::frontend
