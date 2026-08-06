#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_manifest.h"
#include "ogplay/loader/apk_native.h"
#include "ogplay/runtime/bionic/bionic_module_set.h"
#include "ogplay/session/profile_native_calls.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ApkProfileMatch final {
    loader::AndroidManifestFacts manifest;
    loader::ApkNativeLibrary library;
    const TitleProfile* profile{};
};

struct ApkProfileLaunch final {
    ApkProfileMatch match;
    runtime::BionicModuleSet modules;
    std::vector<ProfileNativeCallTarget> native_calls;
};

[[nodiscard]] std::optional<ApkProfileMatch> MatchApkTitleProfile(
    const loader::AndroidManifestFacts& manifest,
    std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles);

[[nodiscard]] std::optional<ApkProfileMatch> MatchApkTitleProfile(
    std::span<const std::byte> apk_bytes, const loader::ApkArchive& archive,
    const TitleProfileCatalog& profiles);

[[nodiscard]] std::optional<ApkProfileLaunch> PrepareApkProfileLaunch(
    const loader::AndroidManifestFacts& manifest,
    std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles,
    std::span<const runtime::BionicModuleSource> system_libraries);

[[nodiscard]] std::optional<ApkProfileLaunch> PrepareApkProfileLaunch(
    std::span<const std::byte> apk_bytes, const loader::ApkArchive& archive,
    const TitleProfileCatalog& profiles,
    std::span<const runtime::BionicModuleSource> system_libraries);

}  // namespace ogplay::session
