// Guest filesystem assembly for run-apk (ADR-0020): the Profile's read-only
// base layers first, then the per-title save sandbox on top.

#include "run_apk_vfs.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/core/encoding.h"
#include "ogplay/frontend/user_data_dir.h"
#include "ogplay/hal/host_environment.h"
#include "ogplay/session/profile_vfs.h"

namespace ogplay::frontend {
namespace {

constexpr core::RateLimitPolicy kUnrestrictedLog{
    .mode = core::RateLimitMode::none};
constexpr std::size_t kArchiveBlockBytes = 64U * 1024U;

class ArchiveBlockCache final {
public:
    explicit ArchiveBlockCache(runtime::VirtualFileSystem& filesystem)
        : filesystem_(filesystem) {}

    struct Block final {
        std::vector<std::byte> bytes;
        std::shared_ptr<const runtime::VfsResourceReservation> reservation;
    };

    [[nodiscard]] std::shared_ptr<const Block> Get(
        const std::string& name, const std::uint64_t block,
        const std::size_t size,
        const std::function<void(std::span<std::byte>)>& load) {
        const Key key{name, block};
        std::shared_ptr<const runtime::VfsResourceReservation> reservation;
        {
            std::scoped_lock lock(mutex_);
            const auto found = entries_.find(key);
            if (found != entries_.end()) {
                lru_.splice(lru_.begin(), lru_, found->second.lru);
                return found->second.block;
            }
            for (;;) {
                try {
                    reservation = filesystem_.ReserveResourceMemory(size);
                    break;
                } catch (const runtime::VfsError& error) {
                    if (error.ErrorNumber() != 28 || lru_.empty()) throw;
                    const auto victim = std::prev(lru_.end());
                    entries_.erase(*victim);
                    lru_.erase(victim);
                }
            }
        }
        auto loaded = std::make_shared<Block>();
        loaded->reservation = std::move(reservation);
        loaded->bytes.resize(size);
        load(loaded->bytes);
        std::scoped_lock lock(mutex_);
        if (const auto found = entries_.find(key); found != entries_.end()) {
            lru_.splice(lru_.begin(), lru_, found->second.lru);
            return found->second.block;
        }
        lru_.push_front(key);
        entries_.emplace(key, Entry{loaded, lru_.begin()});
        return loaded;
    }

private:
    using Key = std::pair<std::string, std::uint64_t>;
    struct Entry final {
        std::shared_ptr<const Block> block;
        std::list<Key>::iterator lru;
    };
    runtime::VirtualFileSystem& filesystem_;
    std::mutex mutex_;
    std::list<Key> lru_;
    std::map<Key, Entry> entries_;
};

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
            root.string());
    }
    if (matches.empty()) return std::string(package);
    if (matches.size() == 1U) return matches.front();
    std::sort(matches.begin(), matches.end());
    throw std::runtime_error(
        "multiple save sandbox installations exist for package " +
        std::string(package) + "; pass --installation-id <id>");
}

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
    // Android 4.4 exposes the emulated primary volume through both names.
    // Canonicalizing the storage path preserves shared file and save state.
    filesystem.AddPathAlias("/storage/emulated/0", "/sdcard");
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

static void MountArchive(const session::TitleProfile& profile,
                  const session::ProfileSource profile_source,
                  const runtime::VfsSource vfs_source,
                  std::shared_ptr<const std::vector<std::byte>> apk_bytes,
                  const loader::ApkArchive& archive,
                  runtime::VirtualFileSystem& filesystem) {
    if (!profile.data.has_value()) return;
    const auto cache = std::make_shared<ArchiveBlockCache>(filesystem);
    const auto archive_owner = std::make_shared<const loader::ApkArchive>(archive);
    for (const auto& mount : profile.data->mounts) {
        if (mount.source != profile_source) continue;
        std::vector<runtime::VfsLazyMountEntry> entries;
        entries.reserve(archive.entries.size());
        for (const auto& entry : archive.entries) {
            struct StoredState final {
                std::mutex mutex;
                bool verified{};
                std::uint64_t data_offset{};
            };
            const auto name = entry.name;
            const auto state = std::make_shared<StoredState>();
            entries.push_back({
                name, entry.uncompressed_size,
                [apk_bytes, archive_owner, name] {
                    return loader::ReadApkEntry(*apk_bytes, *archive_owner, name);
                },
                [apk_bytes, archive_owner, name, entry, state, cache](
                    const std::uint64_t offset,
                    const std::span<std::byte> destination,
                    const std::stop_token stop) {
                    if (stop.stop_requested()) return std::size_t{};
                    if (entry.compression_method != 0U) {
                        if (offset >= entry.uncompressed_size || destination.empty())
                            return std::size_t{};
                        const auto total = static_cast<std::size_t>(
                            std::min<std::uint64_t>(destination.size(),
                                entry.uncompressed_size - offset));
                        std::size_t copied{};
                        while (copied < total) {
                            if (stop.stop_requested()) return copied;
                            const auto absolute = offset + copied;
                            const auto block = absolute / kArchiveBlockBytes;
                            const auto within = static_cast<std::size_t>(
                                absolute % kArchiveBlockBytes);
                            const auto block_offset = block * kArchiveBlockBytes;
                            const auto block_size = static_cast<std::size_t>(
                                std::min<std::uint64_t>(kArchiveBlockBytes,
                                    entry.uncompressed_size - block_offset));
                            const auto bytes = cache->Get(
                                name, block, block_size,
                                [&](const std::span<std::byte> output) {
                                    const auto got = loader::ReadApkEntryRange(
                                        *apk_bytes, *archive_owner, name,
                                        block_offset, output, stop);
                                    if (got != output.size())
                                        throw std::runtime_error("APK block read was short");
                                });
                            const auto count = std::min(total - copied,
                                                       block_size - within);
                            std::copy_n(bytes->bytes.begin() +
                                            static_cast<std::ptrdiff_t>(within),
                                        count, destination.begin() +
                                            static_cast<std::ptrdiff_t>(copied));
                            copied += count;
                        }
                        return copied;
                    }
                    std::scoped_lock lock(state->mutex);
                    if (!state->verified) {
                        const auto count = loader::ReadApkEntryRange(
                            *apk_bytes, *archive_owner, name, offset, destination,
                            stop);
                        state->data_offset = loader::StoredApkEntryDataOffset(
                            *apk_bytes, *archive_owner, name);
                        state->verified = true;
                        return count;
                    }
                    const auto available = entry.uncompressed_size - offset;
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>(destination.size(), available));
                    const auto begin = state->data_offset + offset;
                    std::copy_n(apk_bytes->begin() +
                                    static_cast<std::ptrdiff_t>(begin),
                                count, destination.begin());
                    return count;
                },
            });
        }
        filesystem.MountLazyReadOnly(vfs_source, mount.guest, entries);
    }
}

void MountApkArchive(const session::TitleProfile& profile,
                     std::shared_ptr<const std::vector<std::byte>> apk_bytes,
                     const loader::ApkArchive& archive,
                     runtime::VirtualFileSystem& filesystem) {
    MountArchive(profile, session::ProfileSource::apk, runtime::VfsSource::apk,
                 std::move(apk_bytes), archive, filesystem);
}

void MountObbArchive(const session::TitleProfile& profile,
                     std::shared_ptr<const std::vector<std::byte>> obb_bytes,
                     const loader::ApkArchive& archive,
                     runtime::VirtualFileSystem& filesystem) {
    MountArchive(profile, session::ProfileSource::obb, runtime::VfsSource::obb,
                 std::move(obb_bytes), archive, filesystem);
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
            "cannot create the save sandbox root at " + session.root.string());
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
            "cannot open the save sandbox at " + session.root.string() + ": " +
            error.what() +
            " (back up and clear that directory, or pass "
            "--ephemeral-sandbox)");
    }
    return session;
}

void AttachSandbox(const SandboxSession& sandbox,
                   const session::TitleProfile& profile,
                   runtime::VirtualFileSystem& filesystem,
                   core::Logger& logger) {
    const auto writable_roots = session::ProfileWritableRoots(profile);
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
