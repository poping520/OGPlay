#include "ogplay/app/apk_inspect.h"

#include <fstream>
#include <set>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_native.h"
#include "profiles_private.h"

namespace ogplay::app {
namespace {

[[nodiscard]] std::vector<std::byte> ReadApkBytes(
    const std::filesystem::path& path) {
    constexpr std::uintmax_t maximum_apk_bytes = 1024ULL * 1024ULL * 1024ULL;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > maximum_apk_bytes) {
        throw LibraryError(LibraryErrorCode::not_found,
                            "selected APK is unavailable or outside the size limit", path);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw LibraryError(LibraryErrorCode::io_error,
                            "selected APK cannot be opened", path);
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw LibraryError(LibraryErrorCode::io_error,
                            "selected APK could not be read completely", path);
    }
    return bytes;
}

}  // namespace

ApkImportAnalysis AnalyzeApkImport(
    const std::span<const std::byte> apk_bytes,
    const std::filesystem::path& source_apk,
    const ProfileCatalog& profiles) {
    if (source_apk.empty()) {
        throw LibraryError(LibraryErrorCode::invalid_argument,
                            "selected APK path must not be empty");
    }
    auto visuals = ExtractApkApplicationVisuals(apk_bytes);
    const auto archive = loader::ParseApkArchive(apk_bytes);
    const auto libraries = loader::ReadApkArmNativeLibraries(apk_bytes, archive);
    const auto selection = session::SelectApkCompatibilityProfile(
        loader::ReadAndroidManifest(apk_bytes, archive), libraries, ProfileAccess::Get(profiles));
    ApkImportAnalysis result{
        .source_apk = std::filesystem::absolute(source_apk).lexically_normal(),
        .manifest = std::move(visuals.manifest),
        .display_name = std::move(visuals.display_name),
        .icon_png = std::move(visuals.icon_png),
        .visual_fallbacks = std::move(visuals.fallbacks),
    };
    std::set<std::string> abis;
    for (const auto& entry : archive.entries) {
        if (!entry.name.starts_with("lib/") || !entry.name.ends_with(".so")) continue;
        const auto end = entry.name.find('/', 4);
        if (end != std::string::npos) abis.insert(entry.name.substr(4, end - 4));
    }
    result.profile_notes = selection.applicability_notes;
    result.abis.assign(abis.begin(), abis.end());
    if (selection.profile != nullptr) {
        result.profile =
            ProfileAccess::Summary(*selection.profile);
    }
    return result;
}

ApkImportAnalysis AnalyzeApkImportFile(
    const std::filesystem::path& source_apk,
    const ProfileCatalog& profiles) {
    const auto bytes = ReadApkBytes(source_apk);
    return AnalyzeApkImport(bytes, source_apk, profiles);
}

LibraryImport BuildLibraryImport(
    const ApkImportAnalysis& analysis,
    std::optional<std::filesystem::path> external_dir,
    std::string imported_at, std::string external_guest_dir,
    const ExternalDataMode external_mode) {
    ValidateExternalGuestDirectory(external_guest_dir);
    if (imported_at.empty()) {
        throw LibraryError(LibraryErrorCode::invalid_argument,
                            "import timestamp must not be empty");
    }
    if (external_dir.has_value()) {
        *external_dir = std::filesystem::absolute(*external_dir).lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_directory(*external_dir, error) || error) {
            throw LibraryError(LibraryErrorCode::not_found,
                                "selected external data directory is unavailable",
                                *external_dir);
        }
    }
    const bool owned = external_dir.has_value() && external_mode == ExternalDataMode::copy;
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
        .external_guest_dir = std::move(external_guest_dir),
        .external_owned = owned,
    };
    return {analysis.source_apk, std::move(metadata), analysis.icon_png};
}

ApkInspection InspectApk(const std::filesystem::path& path, const ProfileCatalog& profiles) {
    return AnalyzeApkImportFile(path, profiles);
}
}  // namespace ogplay::app
