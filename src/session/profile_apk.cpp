#include "ogplay/session/profile_apk.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::session {
namespace {

[[nodiscard]] bool SameAbi(const ProfileAbi profile,
                           const loader::AndroidArmAbi library) noexcept {
    return (profile == ProfileAbi::armeabi &&
            library == loader::AndroidArmAbi::armeabi) ||
           (profile == ProfileAbi::armeabi_v7a &&
            library == loader::AndroidArmAbi::armeabi_v7a);
}

}  // namespace

namespace {

ApkProfileSummary SummarizeProfile(const TitleProfile& profile) {
    bool requires_external{};
    if (profile.data.has_value()) {
        requires_external = std::any_of(
            profile.data->mounts.begin(), profile.data->mounts.end(),
            [](const ProfileMount& mount) {
                return mount.source == ProfileSource::external && mount.required;
            });
    }
    return {profile.identity.package, profile.identity.name, requires_external};
}

}  // namespace

ApkProfileSummary SummarizeApkProfileMatch(const ApkProfileMatch& match) {
    if (match.profile == nullptr) {
        throw TitleProfileError("matched APK Profile has no profile");
    }
    return SummarizeProfile(*match.profile);
}

std::optional<ApkProfileSummary> FindApkProfileSummary(
    const TitleProfileCatalog& profiles, const std::string_view profile_id) {
    std::optional<ApkProfileSummary> result;
    for (const auto& profile : profiles.Profiles()) {
        if (profile.identity.package != profile_id) continue;
        if (result.has_value()) {
            throw TitleProfileError("Profile catalog contains duplicate profile id: " +
                                    std::string(profile_id));
        }
        result = SummarizeProfile(profile);
    }
    return result;
}

std::optional<ApkProfileMatch> MatchApkTitleProfile(
    const loader::AndroidManifestFacts& manifest,
    const std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles) {
    if (libraries.empty()) {
        throw TitleProfileError("APK has no supported ARM native library");
    }
    std::optional<ApkProfileMatch> result;
    for (const auto& library : libraries) {
        if (library.image.empty()) {
            throw TitleProfileError("APK native library image is empty: " +
                                    library.entry_name);
        }
        const TitleIdentity identity{
            manifest.package, manifest.version_code, library.sha256};
        const auto* profile = profiles.Match(identity);
        if (profile == nullptr) continue;
        if (!SameAbi(profile->identity.abi, library.abi)) {
            throw TitleProfileError(
                "exact APK profile ABI does not match native library: " +
                library.entry_name);
        }
        if (result.has_value()) {
            throw TitleProfileError(
                "APK matches multiple profiled native libraries; main library is ambiguous");
        }
        result = ApkProfileMatch{manifest, library, profile};
    }
    return result;
}

std::optional<ApkProfileMatch> MatchApkTitleProfile(
    const std::span<const std::byte> apk_bytes,
    const loader::ApkArchive& archive, const TitleProfileCatalog& profiles) {
    const auto manifest = loader::ReadAndroidManifest(apk_bytes, archive);
    const auto libraries = loader::ReadApkArmNativeLibraries(apk_bytes, archive);
    return MatchApkTitleProfile(manifest, libraries, profiles);
}

std::optional<ApkProfileLaunch> PrepareApkProfileLaunch(
    const loader::AndroidManifestFacts& manifest,
    const std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles,
    const std::span<const runtime::BionicModuleSource> system_libraries) {
    auto match = MatchApkTitleProfile(manifest, libraries, profiles);
    if (!match.has_value()) return std::nullopt;
    if (match->profile == nullptr) {
        throw TitleProfileError("matched APK Profile has no profile");
    }
    auto modules = runtime::BuildBionicModuleSet(
        runtime::SelectBionicProfile(match->profile->runtime.api_level),
        match->library.basename, match->library.image, system_libraries);
    return ApkProfileLaunch{std::move(*match), std::move(modules)};
}

std::optional<ApkProfileLaunch> PrepareApkProfileLaunch(
    const std::span<const std::byte> apk_bytes,
    const loader::ApkArchive& archive, const TitleProfileCatalog& profiles,
    const std::span<const runtime::BionicModuleSource> system_libraries) {
    const auto manifest = loader::ReadAndroidManifest(apk_bytes, archive);
    const auto libraries = loader::ReadApkArmNativeLibraries(apk_bytes, archive);
    return PrepareApkProfileLaunch(manifest, libraries, profiles, system_libraries);
}

}  // namespace ogplay::session
