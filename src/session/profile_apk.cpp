#include "ogplay/session/profile_apk.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "ogplay/loader/elf.h"

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
    std::vector<ProfileNativeCallTarget> native_calls;
    if (!match->profile->runtime.native_calls.empty()) {
        const auto& root = modules.Modules().front();
        const auto image = loader::ParseElf32Arm(root.image);
        const auto symbols = loader::ReadElf32SymbolTable(root.image, image);
        native_calls = ResolveProfileNativeCalls(
            match->profile->runtime.native_calls, symbols, root.load_bias);
    }
    return ApkProfileLaunch{std::move(*match), std::move(modules),
                            std::move(native_calls)};
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
