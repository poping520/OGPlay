#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ogplay/frontend/gui_model.h"
#include "ogplay/frontend/gui_visuals.h"
#include "ogplay/runtime/integration/host_image_decode.h"

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
    for (const char value : text) out.push_back(static_cast<std::byte>(value));
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

using ZipFile = std::pair<std::string, std::vector<std::byte>>;

std::vector<std::byte> StoredZip(const std::vector<ZipFile>& files) {
    struct Central final {
        std::string name;
        std::uint32_t crc{};
        std::uint32_t size{};
        std::uint32_t offset{};
    };
    std::vector<std::byte> result;
    std::vector<Central> central;
    for (const auto& [name, data] : files) {
        const auto offset = static_cast<std::uint32_t>(result.size());
        const auto crc = Crc32(data);
        Push32(result, 0x04034b50); Push16(result, 20); Push16(result, 0);
        Push16(result, 0); Push16(result, 0); Push16(result, 0); Push32(result, crc);
        Push32(result, static_cast<std::uint32_t>(data.size()));
        Push32(result, static_cast<std::uint32_t>(data.size()));
        Push16(result, static_cast<std::uint16_t>(name.size())); Push16(result, 0);
        PushText(result, name); result.insert(result.end(), data.begin(), data.end());
        central.push_back({name, crc, static_cast<std::uint32_t>(data.size()), offset});
    }
    const auto central_offset = static_cast<std::uint32_t>(result.size());
    for (const auto& entry : central) {
        Push32(result, 0x02014b50); Push16(result, 20); Push16(result, 20);
        Push16(result, 0); Push16(result, 0); Push16(result, 0); Push16(result, 0);
        Push32(result, entry.crc); Push32(result, entry.size); Push32(result, entry.size);
        Push16(result, static_cast<std::uint16_t>(entry.name.size()));
        Push16(result, 0); Push16(result, 0); Push16(result, 0); Push16(result, 0);
        Push32(result, 0); Push32(result, entry.offset); PushText(result, entry.name);
    }
    const auto central_size = static_cast<std::uint32_t>(result.size()) - central_offset;
    Push32(result, 0x06054b50); Push16(result, 0); Push16(result, 0);
    Push16(result, static_cast<std::uint16_t>(central.size()));
    Push16(result, static_cast<std::uint16_t>(central.size()));
    Push32(result, central_size); Push32(result, central_offset); Push16(result, 0);
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

struct XmlAttribute final {
    std::uint32_t name{};
    std::uint32_t raw{0xffffffffU};
    std::uint8_t type{};
    std::uint32_t data{};
    std::uint32_t namespace_index{0xffffffffU};
};

std::vector<std::byte> StartElement(
    const std::uint32_t name, const std::vector<XmlAttribute>& attributes) {
    std::vector<std::byte> chunk;
    Push16(chunk, 0x0102); Push16(chunk, 16);
    Push32(chunk, static_cast<std::uint32_t>(36U + attributes.size() * 20U));
    Push32(chunk, 1); Push32(chunk, 0xffffffffU);
    Push32(chunk, 0xffffffffU); Push32(chunk, name);
    Push16(chunk, 20); Push16(chunk, 20);
    Push16(chunk, static_cast<std::uint16_t>(attributes.size()));
    Push16(chunk, 0); Push16(chunk, 0); Push16(chunk, 0);
    for (const auto& attribute : attributes) {
        Push32(chunk, attribute.namespace_index); Push32(chunk, attribute.name);
        Push32(chunk, attribute.raw); Push16(chunk, 8); chunk.push_back(std::byte{0});
        chunk.push_back(static_cast<std::byte>(attribute.type)); Push32(chunk, attribute.data);
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

void Append(std::vector<std::byte>& out, const std::vector<std::byte>& value) {
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<std::byte> Manifest(const bool visuals, const bool literal_label = false,
                                const std::uint32_t icon_id = 0x7f010000U,
                                const std::uint32_t label_id = 0x7f010001U,
                                const std::string_view literal = "Example Game") {
    const std::vector<std::string> strings{
        "manifest", "package", "versionCode", "application", "icon", "label",
        "org.example.game", std::string(literal),
        "http://schemas.android.com/apk/res/android"};
    std::vector<std::byte> result;
    Push16(result, 0x0003); Push16(result, 8); Push32(result, 0);
    Append(result, Utf8Pool(strings));
    Append(result, StartElement(0, {{1, 6, 0x03, 6},
                                    {2, 0xffffffffU, 0x10, 7, 8}}));
    std::vector<XmlAttribute> attributes;
    if (visuals) {
        attributes.push_back({4, 0xffffffffU, 0x01, icon_id, 8});
        attributes.push_back(literal_label
                                 ? XmlAttribute{5, 7, 0x03, 7, 8}
                                 : XmlAttribute{5, 0xffffffffU, 0x01, label_id, 8});
    }
    Append(result, StartElement(3, attributes)); Append(result, EndElement(3));
    Append(result, EndElement(0));
    Patch32(result, 4, static_cast<std::uint32_t>(result.size()));
    return result;
}

std::vector<std::byte> Resources(
    const std::string& icon_path = "res/drawable/icon.png",
    const std::string& label = "Resource Game") {
    const auto global = Utf8Pool({icon_path, label});
    const auto type_pool = Utf8Pool({"drawable"});
    const auto key_pool = Utf8Pool({"icon", "app_name"});

    std::vector<std::byte> type;
    Push16(type, 0x0201); Push16(type, 56); Push32(type, 0);
    type.push_back(std::byte{1}); type.push_back(std::byte{0}); Push16(type, 0);
    Push32(type, 2); Push32(type, 0); Push32(type, 36);
    for (int index = 0; index < 32; ++index) type.push_back(std::byte{0});
    Push32(type, 0); Push32(type, 16);
    Patch32(type, 16, static_cast<std::uint32_t>(type.size()));
    for (std::uint32_t index = 0; index < 2; ++index) {
        Push16(type, 8); Push16(type, 0); Push32(type, index);
        Push16(type, 8); type.push_back(std::byte{0}); type.push_back(std::byte{0x03});
        Push32(type, index);
    }
    Patch32(type, 4, static_cast<std::uint32_t>(type.size()));

    std::vector<std::byte> package;
    Push16(package, 0x0200); Push16(package, 288); Push32(package, 0);
    Push32(package, 0x7f);
    const std::string_view package_name = "org.example.game";
    for (std::size_t index = 0; index < 128; ++index) {
        Push16(package, index < package_name.size()
                            ? static_cast<std::uint8_t>(package_name[index]) : 0);
    }
    const auto type_offset = package.size(); Push32(package, 0); Push32(package, 0);
    const auto key_offset = package.size(); Push32(package, 0); Push32(package, 0);
    while (package.size() < 288) package.push_back(std::byte{0});
    Patch32(package, type_offset, static_cast<std::uint32_t>(package.size()));
    Append(package, type_pool);
    Patch32(package, key_offset, static_cast<std::uint32_t>(package.size()));
    Append(package, key_pool); Append(package, type);
    Patch32(package, 4, static_cast<std::uint32_t>(package.size()));

    std::vector<std::byte> table;
    Push16(table, 0x0002); Push16(table, 12); Push32(table, 0); Push32(table, 1);
    Append(table, global); Append(table, package);
    Patch32(table, 4, static_cast<std::uint32_t>(table.size()));
    return table;
}

std::vector<std::byte> OnePixelPng() {
    constexpr std::array<std::uint8_t, 68> bytes{
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,
        0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x04,0x00,0x00,
        0x00,0xb5,0x1c,0x0c,0x02,0x00,0x00,0x00,0x0b,0x49,0x44,0x41,0x54,0x78,
        0xda,0x63,0x64,0xf8,0x0f,0x00,0x01,0x05,0x01,0x01,0x27,0x18,0xe3,0x66,
        0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
    std::vector<std::byte> result;
    result.reserve(bytes.size());
    for (const auto value : bytes) result.push_back(static_cast<std::byte>(value));
    return result;
}

class TempDirectory final {
public:
    TempDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("ogplay-gui-visuals-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() { std::error_code error; std::filesystem::remove_all(path_, error); }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

void WriteBytes(const std::filesystem::path& path,
                const std::span<const std::byte> bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

}  // namespace

TEST_CASE("launcher extracts resource label and normalized PNG then caches it") {
    const auto apk = StoredZip({{"AndroidManifest.xml", Manifest(true)},
                                {"resources.arsc", Resources()},
                                {"res/drawable/icon.png", OnePixelPng()}});
    const auto visuals = ogplay::frontend::ExtractApkApplicationVisuals(apk);
    CHECK(visuals.display_name == "Resource Game");
    CHECK(visuals.fallbacks.empty());
    REQUIRE_FALSE(visuals.icon_png.empty());
    const auto decoded = ogplay::runtime::DecodeImageToArgb(visuals.icon_png);
    REQUIRE(decoded.has_value());
    CHECK(decoded->width == 128);
    CHECK(decoded->height == 128);

    TempDirectory temporary;
    const auto source = temporary.Path() / "source.apk";
    WriteBytes(source, apk);
    ogplay::frontend::LibraryStore store(temporary.Path() / "root");
    store.Import({source,
                  {visuals.manifest.package, visuals.display_name,
                   visuals.manifest.version_code, "", "2026-08-13T00:00:00Z",
                   std::nullopt, std::nullopt},
                  visuals.icon_png});
    CHECK(std::filesystem::file_size(store.EntriesRoot() / visuals.manifest.package /
                                     "icon.png") == visuals.icon_png.size());
}

TEST_CASE("launcher visual extraction uses literal and explicit fallbacks") {
    const auto literal = StoredZip({{"AndroidManifest.xml", Manifest(true, true)}});
    const auto literal_result = ogplay::frontend::ExtractApkApplicationVisuals(literal);
    CHECK(literal_result.display_name == "Example Game");
    CHECK(literal_result.Used(
        ogplay::frontend::ApplicationVisualFallback::icon_resources_unavailable));
    CHECK_FALSE(literal_result.Used(
        ogplay::frontend::ApplicationVisualFallback::label_resources_unavailable));

    const auto empty_label = StoredZip({{"AndroidManifest.xml", Manifest(
        true, true, 0x7f010000U, 0x7f010001U, "")}});
    const auto empty_result =
        ogplay::frontend::ExtractApkApplicationVisuals(empty_label);
    CHECK(empty_result.display_name == "org.example.game");
    CHECK(empty_result.Used(
        ogplay::frontend::ApplicationVisualFallback::label_literal_empty));

    const auto missing = StoredZip({{"AndroidManifest.xml", Manifest(false)}});
    const auto missing_result = ogplay::frontend::ExtractApkApplicationVisuals(missing);
    CHECK(missing_result.display_name == "org.example.game");
    CHECK(missing_result.icon_png.empty());
    CHECK(missing_result.Used(
        ogplay::frontend::ApplicationVisualFallback::icon_attribute_missing));
    CHECK(missing_result.Used(
        ogplay::frontend::ApplicationVisualFallback::label_attribute_missing));
}

TEST_CASE("launcher visual extraction rejects damaged APK identity") {
    const std::vector<std::byte> invalid{std::byte{1}, std::byte{2}};
    CHECK_THROWS(static_cast<void>(
        ogplay::frontend::ExtractApkApplicationVisuals(invalid)));
}

TEST_CASE("launcher visual extraction records every resource and image fallback") {
    const auto missing_ids = StoredZip({
        {"AndroidManifest.xml", Manifest(true, false, 0x7f010002U,
                                          0x7f010003U)},
        {"resources.arsc", Resources()}});
    const auto missing = ogplay::frontend::ExtractApkApplicationVisuals(missing_ids);
    CHECK(missing.Used(
        ogplay::frontend::ApplicationVisualFallback::icon_resource_missing));
    CHECK(missing.Used(
        ogplay::frontend::ApplicationVisualFallback::label_resource_missing));

    const auto unsupported = StoredZip({
        {"AndroidManifest.xml", Manifest(true)},
        {"resources.arsc", Resources("res/drawable/icon.xml")}});
    CHECK(ogplay::frontend::ExtractApkApplicationVisuals(unsupported).Used(
        ogplay::frontend::ApplicationVisualFallback::icon_path_unsupported));

    const auto absent_entry = StoredZip({
        {"AndroidManifest.xml", Manifest(true)},
        {"resources.arsc", Resources()}});
    CHECK(ogplay::frontend::ExtractApkApplicationVisuals(absent_entry).Used(
        ogplay::frontend::ApplicationVisualFallback::icon_entry_unavailable));

    const auto bad_image = StoredZip({
        {"AndroidManifest.xml", Manifest(true)},
        {"resources.arsc", Resources()},
        {"res/drawable/icon.png", {std::byte{1}, std::byte{2}}}});
    CHECK(ogplay::frontend::ExtractApkApplicationVisuals(bad_image).Used(
        ogplay::frontend::ApplicationVisualFallback::icon_decode_failed));
}
