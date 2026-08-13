#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/frontend/gui_import.h"

namespace {

void Push16(std::vector<std::byte>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value));
    out.push_back(static_cast<std::byte>(value >> 8U));
}

void Push32(std::vector<std::byte>& out, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>(value >> shift));
    }
}

void Patch32(std::vector<std::byte>& out, const std::size_t offset,
             const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out[offset + shift / 8U] = static_cast<std::byte>(value >> shift);
    }
}

void PushText(std::vector<std::byte>& out, const std::string_view text) {
    for (const auto value : text) out.push_back(static_cast<std::byte>(value));
}

std::uint32_t Crc32(const std::span<const std::byte> bytes) {
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint8_t>(byte);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::vector<std::byte> StoredZip(const std::string_view name,
                                 const std::vector<std::byte>& payload) {
    const auto crc = Crc32(payload);
    std::vector<std::byte> result;
    Push32(result, 0x04034b50); Push16(result, 20); Push16(result, 0);
    Push16(result, 0); Push16(result, 0); Push16(result, 0); Push32(result, crc);
    Push32(result, static_cast<std::uint32_t>(payload.size()));
    Push32(result, static_cast<std::uint32_t>(payload.size()));
    Push16(result, static_cast<std::uint16_t>(name.size())); Push16(result, 0);
    PushText(result, name); result.insert(result.end(), payload.begin(), payload.end());
    const auto central_offset = static_cast<std::uint32_t>(result.size());
    Push32(result, 0x02014b50); Push16(result, 20); Push16(result, 20);
    Push16(result, 0); Push16(result, 0); Push16(result, 0); Push16(result, 0);
    Push32(result, crc); Push32(result, static_cast<std::uint32_t>(payload.size()));
    Push32(result, static_cast<std::uint32_t>(payload.size()));
    Push16(result, static_cast<std::uint16_t>(name.size()));
    Push16(result, 0); Push16(result, 0); Push16(result, 0); Push16(result, 0);
    Push32(result, 0); Push32(result, 0); PushText(result, name);
    const auto central_size = static_cast<std::uint32_t>(result.size()) - central_offset;
    Push32(result, 0x06054b50); Push16(result, 0); Push16(result, 0);
    Push16(result, 1); Push16(result, 1); Push32(result, central_size);
    Push32(result, central_offset); Push16(result, 0);
    return result;
}

std::vector<std::byte> Utf8Pool(const std::vector<std::string>& strings) {
    std::vector<std::byte> data;
    std::vector<std::uint32_t> offsets;
    for (const auto& value : strings) {
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
        data.push_back(static_cast<std::byte>(value.size()));
        data.push_back(static_cast<std::byte>(value.size()));
        PushText(data, value); data.push_back(std::byte{0});
    }
    while (data.size() % 4U != 0) data.push_back(std::byte{0});
    std::vector<std::byte> pool;
    Push16(pool, 0x0001); Push16(pool, 28); Push32(pool, 0);
    Push32(pool, static_cast<std::uint32_t>(strings.size()));
    Push32(pool, 0); Push32(pool, 0x100); Push32(pool, 0); Push32(pool, 0);
    for (const auto offset : offsets) Push32(pool, offset);
    Patch32(pool, 20, static_cast<std::uint32_t>(pool.size()));
    pool.insert(pool.end(), data.begin(), data.end());
    Patch32(pool, 4, static_cast<std::uint32_t>(pool.size()));
    return pool;
}

std::vector<std::byte> StartElement(
    const std::uint32_t name, const bool manifest) {
    std::vector<std::byte> chunk;
    const auto count = manifest ? 2U : 0U;
    Push16(chunk, 0x0102); Push16(chunk, 16); Push32(chunk, 36U + count * 20U);
    Push32(chunk, 1); Push32(chunk, 0xffffffffU);
    Push32(chunk, 0xffffffffU); Push32(chunk, name);
    Push16(chunk, 20); Push16(chunk, 20); Push16(chunk, static_cast<std::uint16_t>(count));
    Push16(chunk, 0); Push16(chunk, 0); Push16(chunk, 0);
    if (manifest) {
        Push32(chunk, 0xffffffffU); Push32(chunk, 1); Push32(chunk, 4);
        Push16(chunk, 8); chunk.push_back(std::byte{0}); chunk.push_back(std::byte{0x03});
        Push32(chunk, 4);
        Push32(chunk, 5); Push32(chunk, 2); Push32(chunk, 0xffffffffU);
        Push16(chunk, 8); chunk.push_back(std::byte{0}); chunk.push_back(std::byte{0x10});
        Push32(chunk, 9);
    }
    return chunk;
}

std::vector<std::byte> EndElement(const std::uint32_t name) {
    std::vector<std::byte> chunk;
    Push16(chunk, 0x0103); Push16(chunk, 16); Push32(chunk, 24);
    Push32(chunk, 1); Push32(chunk, 0xffffffffU);
    Push32(chunk, 0xffffffffU); Push32(chunk, name);
    return chunk;
}

std::vector<std::byte> MinimalApk() {
    const std::vector<std::string> strings{
        "manifest", "package", "versionCode", "application",
        "org.example.game", "http://schemas.android.com/apk/res/android"};
    std::vector<std::byte> manifest;
    Push16(manifest, 0x0003); Push16(manifest, 8); Push32(manifest, 0);
    const auto append = [&manifest](const std::vector<std::byte>& value) {
        manifest.insert(manifest.end(), value.begin(), value.end());
    };
    append(Utf8Pool(strings)); append(StartElement(0, true));
    append(StartElement(3, false)); append(EndElement(3)); append(EndElement(0));
    Patch32(manifest, 4, static_cast<std::uint32_t>(manifest.size()));
    return StoredZip("AndroidManifest.xml", manifest);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("ogplay-gui-import-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path, error));
    }
    std::filesystem::path path;
};

ogplay::frontend::ApkImportAnalysis Analysis() {
    ogplay::frontend::ApkImportAnalysis analysis;
    analysis.source_apk = std::filesystem::absolute("fixture.apk");
    analysis.manifest.package = "org.example.game";
    analysis.manifest.version_code = 42;
    analysis.manifest.version_name = "1.2.3";
    analysis.display_name = "示例游戏";
    analysis.icon_png = {std::byte{1}, std::byte{2}};
    return analysis;
}

}  // namespace

TEST_CASE("GUI import modal stays alive while a host dialog is pending") {
    CHECK_FALSE(ogplay::frontend::CanDismissImportModal(true));
    CHECK(ogplay::frontend::CanDismissImportModal(false));
}

TEST_CASE("GUI import builds exact metadata without inventing a Profile") {
    const auto request = ogplay::frontend::BuildLibraryImport(
        Analysis(), std::nullopt, "2026-08-13T00:00:00Z");
    CHECK(request.metadata.package == "org.example.game");
    CHECK(request.metadata.display_name == "示例游戏");
    CHECK(request.metadata.version_code == 42);
    CHECK(request.metadata.version_name == "1.2.3");
    CHECK_FALSE(request.metadata.profile_id.has_value());
    CHECK_FALSE(request.metadata.external_dir.has_value());
    CHECK(request.icon_png == std::vector<std::byte>{std::byte{1}, std::byte{2}});
}

TEST_CASE("GUI import analyzes a valid unmatched APK without native libraries") {
    const ogplay::session::TitleProfileCatalog catalog({});
    const auto analysis = ogplay::frontend::AnalyzeApkImport(
        MinimalApk(), "selected.apk", catalog);
    CHECK(analysis.manifest.package == "org.example.game");
    CHECK(analysis.manifest.version_code == 9);
    CHECK(analysis.display_name == "org.example.game");
    CHECK_FALSE(analysis.profile.has_value());
    CHECK(analysis.icon_png.empty());
    CHECK(analysis.visual_fallbacks.size() == 2);
}

TEST_CASE("GUI import accepts optional required external data only as a real directory") {
    TemporaryDirectory temporary;
    const auto external = temporary.path / "external";
    std::filesystem::create_directories(external);
    auto analysis = Analysis();
    analysis.profile = ogplay::session::ApkProfileSummary{
        "org.example.game", "Example", true};

    const auto skipped = ogplay::frontend::BuildLibraryImport(
        analysis, std::nullopt, "2026-08-13T00:00:00Z");
    CHECK(skipped.metadata.profile_id == "org.example.game");
    CHECK_FALSE(skipped.metadata.external_dir.has_value());

    const auto selected = ogplay::frontend::BuildLibraryImport(
        analysis, external, "2026-08-13T00:00:00Z");
    CHECK(selected.metadata.external_dir ==
          std::filesystem::absolute(external).lexically_normal());
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLibraryImport(
                        analysis, temporary.path / "missing",
                        "2026-08-13T00:00:00Z")),
                    ogplay::frontend::GuiModelError);
}

TEST_CASE("GUI import rejects missing APK and empty timestamp explicitly") {
    const ogplay::session::TitleProfileCatalog catalog({});
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::AnalyzeApkImportFile(
                        "missing.apk", catalog)),
                    ogplay::frontend::GuiModelError);
    CHECK_THROWS_AS(static_cast<void>(ogplay::frontend::BuildLibraryImport(
                        Analysis(), std::nullopt, "")),
                    ogplay::frontend::GuiModelError);
}
