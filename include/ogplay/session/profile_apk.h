#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_manifest.h"
#include "ogplay/loader/apk_native.h"
#include "ogplay/session/title_profile.h"

namespace ogplay::session {

struct ApkProfileMatch final {
    loader::AndroidManifestFacts manifest;
    loader::ApkNativeLibrary library;
    const TitleProfile* profile{};
};

[[nodiscard]] std::optional<ApkProfileMatch> MatchApkTitleProfile(
    const loader::AndroidManifestFacts& manifest,
    std::span<const loader::ApkNativeLibrary> libraries,
    const TitleProfileCatalog& profiles);

[[nodiscard]] std::optional<ApkProfileMatch> MatchApkTitleProfile(
    std::span<const std::byte> apk_bytes, const loader::ApkArchive& archive,
    const TitleProfileCatalog& profiles);

}  // namespace ogplay::session
