#include "ogplay/app/apk_inspect.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "ogplay/loader/apk.h"
#include "ogplay/loader/apk_native.h"
#include "locks.h"
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

std::vector<std::string> LibraryStore::RefreshIcons() {
    auto library_lock = RequireLibraryLock(root_);
    std::vector<std::string> refreshed;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(EntriesRoot(), error), end; !error && iterator != end;
         iterator.increment(error)) {
        const auto name = iterator->path().filename().u8string();
        std::error_code entry_error;
        if (name.empty() || name.front() == u8'.' || !iterator->is_directory(entry_error)) continue;
        const auto directory = iterator->path();
        std::vector<std::byte> icon;
        try {
            icon = ExtractApkApplicationVisuals(ReadApkBytes(directory / "game.apk")).icon_png;
        } catch (const std::exception&) {
            continue;  // damaged entries are reported by LoadEntries, not here
        }
        if (icon.empty()) continue;
        const auto target = directory / "icon.png";
        std::ifstream current(target, std::ios::binary);
        const std::vector<char> existing{std::istreambuf_iterator<char>(current), {}};
        if (existing.size() == icon.size() &&
            std::equal(existing.begin(), existing.end(), icon.begin(),
                       [](const char left, const std::byte right) { return static_cast<std::byte>(left) == right; }))
            continue;
        const auto temporary = directory / ".icon.png.refresh";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(icon.data()), static_cast<std::streamsize>(icon.size()));
            if (!output) throw LibraryError(LibraryErrorCode::io_error, "cannot write refreshed icon", temporary);
        }
        std::filesystem::rename(temporary, target, entry_error);
        if (entry_error) {
            std::filesystem::remove(temporary, entry_error);
            throw LibraryError(LibraryErrorCode::io_error, "cannot replace library icon", target);
        }
        const auto key = iterator->path().filename().u8string();
        refreshed.emplace_back(reinterpret_cast<const char*>(key.data()), key.size());
    }
    if (error) throw LibraryError(LibraryErrorCode::io_error, "cannot enumerate game library", EntriesRoot());
    return refreshed;
}

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
