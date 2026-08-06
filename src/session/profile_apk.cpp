#include "ogplay/session/profile_apk.h"

#include <stdexcept>
#include <string>

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

}  // namespace ogplay::session
