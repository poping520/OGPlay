#include "ogplay/session/profile_asset_bundle.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::session {
namespace {

[[nodiscard]] char FoldAscii(const char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

[[nodiscard]] std::string Folded(const std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), FoldAscii);
    return result;
}

[[nodiscard]] bool ValidRelativePath(const std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.back() == '/' || path.find("//") != std::string_view::npos ||
        path.find('\\') != std::string_view::npos) {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin
                                                 : end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

[[nodiscard]] bool ValidGuestRoot(const std::string_view path) {
    return path == "/" ||
           (!path.empty() && path.front() == '/' && path.back() != '/' &&
            ValidRelativePath(path.substr(1)));
}

void ValidateSource(const ProfileSource source) {
    switch (source) {
    case ProfileSource::apk:
    case ProfileSource::obb:
    case ProfileSource::external: return;
    }
    throw ProfileAssetBundleError("Profile asset source is unsupported");
}

}  // namespace

ProfileAssetBundle::ProfileAssetBundle(
    std::vector<ProfileVfsMountInput> vfs_mounts,
    std::vector<ProfileAudioResource> audio_resources)
    : vfs_mounts_(std::move(vfs_mounts)),
      audio_resources_(std::move(audio_resources)) {
    std::set<std::string, std::less<>> guest_roots;
    for (const auto& mount : vfs_mounts_) {
        ValidateSource(mount.source);
        if (!ValidGuestRoot(mount.guest) ||
            !guest_roots.insert(Folded(mount.guest)).second) {
            throw ProfileAssetBundleError(
                "Profile asset VFS root is invalid or duplicated: " +
                mount.guest);
        }
        if (mount.entries.empty()) {
            throw ProfileAssetBundleError(
                "Profile asset VFS mount has no files: " + mount.guest);
        }
        std::set<std::string, std::less<>> entry_paths;
        for (const auto& entry : mount.entries) {
            if (!ValidRelativePath(entry.path) ||
                !entry_paths.insert(Folded(entry.path)).second) {
                throw ProfileAssetBundleError(
                    "Profile asset VFS entry is invalid or duplicated: " +
                    entry.path);
            }
        }
    }

    std::set<std::pair<ProfileSource, std::string>> audio_keys;
    for (const auto& resource : audio_resources_) {
        ValidateSource(resource.source);
        if (!ValidRelativePath(resource.path) || resource.contents.empty()) {
            throw ProfileAssetBundleError(
                "Profile audio asset requires a path and contents");
        }
        if (!audio_keys.emplace(resource.source, resource.path).second) {
            throw ProfileAssetBundleError(
                "Profile audio asset is duplicated: " + resource.path);
        }
    }
}

std::span<const ProfileVfsMountInput>
ProfileAssetBundle::VfsMounts() const noexcept {
    return vfs_mounts_;
}

std::span<const ProfileAudioResource>
ProfileAssetBundle::AudioResources() const noexcept {
    return audio_resources_;
}

}  // namespace ogplay::session
