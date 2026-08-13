#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/frontend/gui_model.h"
#include "ogplay/frontend/gui_visuals.h"
#include "ogplay/session/profile_apk.h"

namespace ogplay::frontend {

// A native file/folder dialog cannot be closed programmatically. The owning
// import modal must therefore stay alive until its callback is delivered.
[[nodiscard]] bool CanDismissImportModal(bool host_dialog_pending) noexcept;

struct ApkImportAnalysis final {
    std::filesystem::path source_apk;
    loader::AndroidManifestFacts manifest;
    std::string display_name;
    std::vector<std::byte> icon_png;
    std::vector<ApplicationVisualFallback> visual_fallbacks;
    std::optional<session::ApkProfileSummary> profile;
};

[[nodiscard]] ApkImportAnalysis AnalyzeApkImport(
    std::span<const std::byte> apk_bytes,
    const std::filesystem::path& source_apk,
    const session::TitleProfileCatalog& profiles);

[[nodiscard]] ApkImportAnalysis AnalyzeApkImportFile(
    const std::filesystem::path& source_apk,
    const session::TitleProfileCatalog& profiles);

// external_dir may be omitted even when required; the entry is imported with
// the documented missing-data badge. A selected path must be an existing
// absolute directory. imported_at is injected so tests never read wall time.
[[nodiscard]] LibraryImport BuildLibraryImport(
    const ApkImportAnalysis& analysis,
    std::optional<std::filesystem::path> external_dir,
    std::string imported_at);

}  // namespace ogplay::frontend
