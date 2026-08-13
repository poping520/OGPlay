#include "ogplay/session/profile_vfs.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ogplay::session {
namespace {

[[nodiscard]] char FoldAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string PathKey(const std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), FoldAscii);
    return result;
}

[[nodiscard]] runtime::VfsSource VfsSource(const ProfileSource source) {
    switch (source) {
    case ProfileSource::apk: return runtime::VfsSource::apk;
    case ProfileSource::obb: return runtime::VfsSource::obb;
    case ProfileSource::external: return runtime::VfsSource::external;
    }
    throw ProfileVfsError("profile mount has an unsupported source");
}

[[nodiscard]] std::string Join(const std::string_view root,
                               const std::string_view relative) {
    std::string result(root);
    if (result.size() != 1 || result.front() != '/') result.push_back('/');
    result.append(relative);
    return result;
}

[[nodiscard]] bool Covers(const std::string_view root,
                          const std::string_view path) {
    const auto folded_root = PathKey(root);
    const auto folded_path = PathKey(path);
    if (folded_root == "/") return !folded_path.empty() && folded_path.front() == '/';
    return folded_path == folded_root ||
           (folded_path.size() > folded_root.size() &&
            folded_path.starts_with(folded_root) &&
            folded_path[folded_root.size()] == '/');
}

[[nodiscard]] const ProfileVfsMountInput* FindInput(
    const ProfileMount& mount, const std::span<const ProfileVfsMountInput> inputs) {
    const auto key = PathKey(mount.guest);
    const auto found = std::find_if(
        inputs.begin(), inputs.end(), [&key](const ProfileVfsMountInput& input) {
            return PathKey(input.guest) == key;
        });
    if (found == inputs.end()) return nullptr;
    if (found->source != mount.source) {
        throw ProfileVfsError("profile mount source does not match its declaration: " +
                              mount.guest);
    }
    return &*found;
}

void ValidateInputs(const ProfileData& data,
                    const std::span<const ProfileVfsMountInput> inputs) {
    std::vector<std::string> declared;
    declared.reserve(data.mounts.size());
    for (const auto& mount : data.mounts) {
        static_cast<void>(VfsSource(mount.source));
        const auto key = PathKey(mount.guest);
        if (std::find(declared.begin(), declared.end(), key) != declared.end()) {
            throw ProfileVfsError("profile contains duplicate VFS mount roots");
        }
        declared.push_back(key);
    }

    std::vector<std::string> supplied;
    supplied.reserve(inputs.size());
    for (const auto& input : inputs) {
        static_cast<void>(VfsSource(input.source));
        const auto key = PathKey(input.guest);
        if (std::find(supplied.begin(), supplied.end(), key) != supplied.end()) {
            throw ProfileVfsError("profile VFS input contains duplicate mount roots");
        }
        supplied.push_back(key);
        if (std::find(declared.begin(), declared.end(), key) == declared.end()) {
            throw ProfileVfsError("profile VFS input is not declared: " + input.guest);
        }
        if (input.entries.empty()) {
            throw ProfileVfsError("profile VFS input has no files: " + input.guest);
        }
    }
}

}  // namespace

std::vector<std::string> ProfileWritableRoots(const TitleProfile& profile) {
    // Both are platform defaults, not per-title declarations: a Profile
    // never has to opt in to keeping its own saves.
    return {"/data/data/" + profile.identity.package, "/sdcard"};
}

ProfileVfsAssembly AssembleProfileVfs(
    const TitleProfile& profile,
    const std::span<const ProfileVfsMountInput> inputs,
    runtime::SandboxStore* sandbox) {
    auto filesystem = std::make_unique<runtime::VirtualFileSystem>();
    const auto attach = [&profile, sandbox](
                            runtime::VirtualFileSystem& target) {
        std::vector<std::string> roots;
        if (sandbox == nullptr) return roots;
        roots = ProfileWritableRoots(profile);
        try {
            target.AttachSandbox(*sandbox, roots);
        } catch (const runtime::VfsError& error) {
            // Persistence never degrades silently to memory: either the
            // sandbox attaches or the session fails to assemble.
            throw ProfileVfsError(
                std::string("profile sandbox attach failed: ") + error.what());
        }
        return roots;
    };
    if (!profile.data.has_value()) {
        if (!inputs.empty()) {
            throw ProfileVfsError("profile has VFS input but no data declaration");
        }
        auto roots = attach(*filesystem);
        return {std::move(filesystem), {}, {}, std::move(roots)};
    }

    const auto& data = *profile.data;
    ValidateInputs(data, inputs);
    std::vector<std::string> mounted_roots;
    mounted_roots.reserve(data.mounts.size());
    for (const auto& mount : data.mounts) {
        const auto* input = FindInput(mount, inputs);
        if (input == nullptr) {
            if (mount.required) {
                throw ProfileVfsError("required profile VFS mount is missing: " +
                                      mount.guest);
            }
            continue;
        }
        try {
            filesystem->Mount(VfsSource(mount.source), mount.guest,
                              input->entries);
        } catch (const runtime::VfsError& error) {
            throw ProfileVfsError("profile VFS mount failed for " + mount.guest +
                                  ": " + error.what());
        }
        mounted_roots.push_back(mount.guest);
    }

    // The overlay goes on after every read-only base layer, so a saved file
    // shadows the shipped one rather than the other way round.
    auto writable_roots = attach(*filesystem);

    if (data.working_directory.has_value()) {
        const auto covered = std::any_of(
            mounted_roots.begin(), mounted_roots.end(),
            [&data](const std::string& root) {
                return Covers(root, *data.working_directory);
            });
        if (!covered) {
            throw ProfileVfsError(
                "profile working directory is not covered by a mounted source");
        }
    }

    std::vector<ProfileManifestStatus> manifest;
    manifest.reserve(data.manifest.size());
    for (const auto& expected : data.manifest) {
        bool present = false;
        for (const auto& root : mounted_roots) {
            try {
                static_cast<void>(filesystem->Stat(Join(root, expected.path)));
                present = true;
                break;
            } catch (const runtime::VfsError& error) {
                if (error.ErrorNumber() != 2) {
                    throw ProfileVfsError("profile manifest check failed: " +
                                          std::string(error.what()));
                }
            }
        }
        if (expected.required && !present) {
            throw ProfileVfsError("required profile manifest file is missing: " +
                                  expected.path);
        }
        manifest.push_back({expected.path, expected.required, present});
    }

    return {std::move(filesystem), data.working_directory,
            std::move(manifest), std::move(writable_roots)};
}

}  // namespace ogplay::session
