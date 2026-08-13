#include "ogplay/frontend/gui_import.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_native.h"

namespace ogplay::frontend {
namespace {

[[nodiscard]] std::vector<std::byte> ReadApkBytes(
    const std::filesystem::path& path) {
    constexpr std::uintmax_t maximum_apk_bytes = 1024ULL * 1024ULL * 1024ULL;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum_apk_bytes) {
        throw GuiModelError(GuiModelErrorCode::not_found,
                            "selected APK is unavailable or outside the size limit", path);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "selected APK cannot be opened", path);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw GuiModelError(GuiModelErrorCode::io_error,
                            "selected APK could not be read completely", path);
    }
    return bytes;
}

}  // namespace

bool CanDismissImportModal(const bool host_dialog_pending) noexcept {
    return !host_dialog_pending;
}

ApkImportAnalysis AnalyzeApkImport(
    const std::span<const std::byte> apk_bytes,
    const std::filesystem::path& source_apk,
    const session::TitleProfileCatalog& profiles) {
    if (source_apk.empty()) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "selected APK path must not be empty");
    }
    auto visuals = ExtractApkApplicationVisuals(apk_bytes);
    const auto archive = loader::ParseApkArchive(apk_bytes);
    const auto libraries = loader::ReadApkArmNativeLibraries(apk_bytes, archive);
    const auto match = libraries.empty()
                           ? std::optional<session::ApkProfileMatch>{}
                           : session::MatchApkTitleProfile(
                                 visuals.manifest, libraries, profiles);
    ApkImportAnalysis result{
        .source_apk = std::filesystem::absolute(source_apk).lexically_normal(),
        .manifest = std::move(visuals.manifest),
        .display_name = std::move(visuals.display_name),
        .icon_png = std::move(visuals.icon_png),
        .visual_fallbacks = std::move(visuals.fallbacks),
    };
    if (match.has_value()) result.profile = session::SummarizeApkProfileMatch(*match);
    return result;
}

ApkImportAnalysis AnalyzeApkImportFile(
    const std::filesystem::path& source_apk,
    const session::TitleProfileCatalog& profiles) {
    const auto bytes = ReadApkBytes(source_apk);
    return AnalyzeApkImport(bytes, source_apk, profiles);
}

LibraryImport BuildLibraryImport(
    const ApkImportAnalysis& analysis,
    std::optional<std::filesystem::path> external_dir,
    std::string imported_at) {
    if (imported_at.empty()) {
        throw GuiModelError(GuiModelErrorCode::invalid_argument,
                            "import timestamp must not be empty");
    }
    if (external_dir.has_value()) {
        *external_dir = std::filesystem::absolute(*external_dir).lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_directory(*external_dir, error) || error) {
            throw GuiModelError(GuiModelErrorCode::not_found,
                                "selected external data directory is unavailable",
                                *external_dir);
        }
    }
    LibraryMetadata metadata{
        .package = analysis.manifest.package,
        .display_name = analysis.display_name,
        .version_code = analysis.manifest.version_code,
        .version_name = analysis.manifest.version_name.value_or(""),
        .imported_at = std::move(imported_at),
        .profile_id = analysis.profile.has_value()
                          ? std::optional<std::string>(analysis.profile->profile_id)
                          : std::nullopt,
        .external_dir = std::move(external_dir),
    };
    return {analysis.source_apk, std::move(metadata), analysis.icon_png};
}

}  // namespace ogplay::frontend
