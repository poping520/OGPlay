#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/app/library.h"
#include "ogplay/app/apk_visuals.h"
#include "ogplay/app/profiles.h"

namespace ogplay::app {

struct ApkImportAnalysis final {
    std::filesystem::path source_apk;
    ManifestSummary manifest;
    std::string display_name;
    std::vector<std::byte> icon_png;
    std::vector<ApplicationVisualFallback> visual_fallbacks;
    std::optional<ProfileSummary> profile;
    std::vector<std::string> abis;
    std::vector<std::string> profile_notes;
};

[[nodiscard]] OGPLAYCORE_API ApkImportAnalysis AnalyzeApkImport(
    std::span<const std::byte> apk_bytes,
    const std::filesystem::path& source_apk,
    const ProfileCatalog& profiles);

[[nodiscard]] OGPLAYCORE_API ApkImportAnalysis AnalyzeApkImportFile(
    const std::filesystem::path& source_apk,
    const ProfileCatalog& profiles);

// external_dir may be omitted even when required; the entry is imported with
// the documented missing-data badge. A selected path must be an existing
// absolute directory. imported_at is injected so tests never read wall time.
[[nodiscard]] OGPLAYCORE_API LibraryImport BuildLibraryImport(
    const ApkImportAnalysis& analysis,
    std::optional<std::filesystem::path> external_dir,
    std::string imported_at);

}  // namespace ogplay::app

namespace ogplay::app {
using ApkInspection = ApkImportAnalysis;
[[nodiscard]] OGPLAYCORE_API ApkInspection InspectApk(const std::filesystem::path& source, const ProfileCatalog& profiles);
}
