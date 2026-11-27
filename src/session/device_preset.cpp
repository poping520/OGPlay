#include "ogplay/session/device_preset.h"

#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>

#include "ogplay/core/text.h"
#include "title_profile_toml.h"

namespace ogplay::session {
namespace {

using Table = detail::TomlValue::Table;

constexpr std::size_t kMaximumBytes = 16U * 1024U;
// PROP_VALUE_MAX - 1: the longest value bionic and SystemProperties accept.
constexpr std::size_t kPropertyBytes = 91;

[[noreturn]] void Fail(const std::string_view field, const std::string_view reason) {
    throw DevicePresetError("device preset " + std::string(field) + ": " + std::string(reason));
}

[[nodiscard]] const detail::TomlValue& Require(const Table& table, const std::string_view key,
                                               const std::string_view field) {
    const auto found = table.find(key);
    if (found == table.end()) Fail(field, "is missing");
    return found->second;
}

[[nodiscard]] const Table& Section(const Table& root, const std::string_view key,
                                   const std::initializer_list<std::string_view> keys) {
    const auto* table = std::get_if<Table>(&Require(root, key, key).value);
    if (table == nullptr) Fail(key, "must be a table");
    detail::ValidateExactTableKeys(*table, key, keys, keys);
    return *table;
}

[[nodiscard]] bool Printable(const std::string_view text, const bool ascii) {
    for (const auto character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || byte == 0x7fU || (ascii && byte > 0x7eU)) return false;
    }
    return true;
}

[[nodiscard]] std::string Text(const Table& table, const std::string_view key, const std::string_view field,
                               const std::size_t maximum, const bool property = false) {
    const auto* value = std::get_if<std::string>(&Require(table, key, field).value);
    if (value == nullptr) Fail(field, "must be a string");
    if (value->empty() || value->size() > maximum || value->front() == ' ' || value->back() == ' ' ||
        !Printable(*value, property))
        Fail(field, property ? "must be 1.." + std::to_string(maximum) + " printable ASCII bytes without padding"
                             : "must be 1.." + std::to_string(maximum) + " bytes of unpadded text");
    return *value;
}

[[nodiscard]] std::uint32_t Integer(const Table& table, const std::string_view key, const std::string_view field,
                                    const std::int64_t minimum, const std::int64_t maximum) {
    const auto* value = std::get_if<std::int64_t>(&Require(table, key, field).value);
    if (value == nullptr) Fail(field, "must be an integer");
    if (*value < minimum || *value > maximum)
        Fail(field, "must be " + std::to_string(minimum) + ".." + std::to_string(maximum));
    return static_cast<std::uint32_t>(*value);
}

void Expect(const std::string& value, const std::string_view expected, const std::string_view field) {
    if (value != expected) Fail(field, "must be " + std::string(expected));
}

[[nodiscard]] bool IsPresetId(const std::string_view id) {
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z' || id.back() == '-') return false;
    char previous{};
    for (const auto character : id) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (!alphanumeric && (character != '-' || previous == '-')) return false;
        previous = character;
    }
    return true;
}

[[nodiscard]] bool IsBuildToken(const std::string_view token) {
    if (token.empty()) return false;
    for (const auto character : token) {
        const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') || character == '.' || character == '_' ||
                             character == '-';
        if (!allowed) return false;
    }
    return true;
}

// <brand>/<name>/<device>:4.4.4/<build id>/<incremental>:user/release-keys
void ValidateFingerprint(const runtime::SimulatedDeviceState::BuildIdentity& build) {
    const auto head = build.brand + "/" + build.name + "/" + build.device + ":4.4.4/";
    constexpr std::string_view tail = ":user/release-keys";
    const std::string_view fingerprint = build.fingerprint;
    if (!fingerprint.starts_with(head) || !fingerprint.ends_with(tail))
        Fail("build.fingerprint", "must be " + head + "<id>/<incremental>" + std::string(tail));
    const auto middle = fingerprint.substr(head.size(), fingerprint.size() - head.size() - tail.size());
    const auto slash = middle.find('/');
    if (slash == std::string_view::npos || !IsBuildToken(middle.substr(0, slash)) ||
        !IsBuildToken(middle.substr(slash + 1U)))
        Fail("build.fingerprint", "build id and incremental must be [A-Za-z0-9._-]+");
}

}  // namespace

DevicePreset LoadDevicePresetText(const std::string_view text, const std::string_view expected_id) {
    if (text.size() > kMaximumBytes) Fail("file", "exceeds 16 KiB");
    if (!core::IsValidUtf8(text)) Fail("file", "is not valid UTF-8");
    Table root;
    try {
        root = detail::ParseDataToml(text);
    } catch (const std::exception& error) {
        Fail("file", error.what());
    }
    detail::ValidateExactTableKeys(
        root, "device preset",
        {"schema", "id", "label", "description", "product", "build", "cpu", "gpu", "display", "memory"},
        {"schema", "id", "label", "description", "product", "build", "cpu", "gpu", "display", "memory"});
    static_cast<void>(Integer(root, "schema", "schema", 1, 1));
    DevicePreset preset;
    preset.id = Text(root, "id", "id", 64);
    if (!IsPresetId(preset.id) || preset.id != expected_id) Fail("id", "must be kebab-case and match the file name");
    preset.label = Text(root, "label", "label", 128);
    preset.description = Text(root, "description", "description", 512);

    const auto& product =
        Section(root, "product", {"manufacturer", "brand", "name", "model", "device", "board", "cpu_abi"});
    auto& build = preset.build;
    build.manufacturer = Text(product, "manufacturer", "product.manufacturer", kPropertyBytes, true);
    build.brand = Text(product, "brand", "product.brand", kPropertyBytes, true);
    build.name = Text(product, "name", "product.name", kPropertyBytes, true);
    build.model = Text(product, "model", "product.model", kPropertyBytes, true);
    build.device = Text(product, "device", "product.device", kPropertyBytes, true);
    build.board = Text(product, "board", "product.board", kPropertyBytes, true);
    build.cpu_abi = Text(product, "cpu_abi", "product.cpu_abi", kPropertyBytes, true);
    Expect(build.cpu_abi, "armeabi-v7a", "product.cpu_abi");

    const auto& build_table = Section(root, "build", {"product", "fingerprint", "version_release", "version_sdk"});
    build.build_product = Text(build_table, "product", "build.product", kPropertyBytes, true);
    build.fingerprint = Text(build_table, "fingerprint", "build.fingerprint", kPropertyBytes, true);
    Expect(Text(build_table, "version_release", "build.version_release", kPropertyBytes, true), "4.4.4",
           "build.version_release");
    static_cast<void>(Integer(build_table, "version_sdk", "build.version_sdk", 19, 19));
    ValidateFingerprint(build);

    const auto& cpu = Section(root, "cpu", {"name", "cores"});
    preset.cpu = {Text(cpu, "name", "cpu.name", 128), Integer(cpu, "cores", "cpu.cores", 1, 32)};
    const auto& gpu = Section(root, "gpu", {"vendor", "renderer"});
    preset.gpu = {Text(gpu, "vendor", "gpu.vendor", 128), Text(gpu, "renderer", "gpu.renderer", 128)};
    const auto& display = Section(root, "display", {"width", "height", "density_dpi"});
    preset.display = {Integer(display, "width", "display.width", 240, 4096),
                      Integer(display, "height", "display.height", 240, 4096),
                      Integer(display, "density_dpi", "display.density_dpi", 72, 960)};
    preset.ram_mb = Integer(Section(root, "memory", {"ram_mb"}), "ram_mb", "memory.ram_mb", 128, 8192);
    return preset;
}

DevicePreset LoadDevicePreset(const std::filesystem::path& directory, const std::string_view id) {
    if (!IsPresetId(id)) throw DevicePresetError("device preset id is invalid: " + std::string(id));
    const auto path = directory / (std::string(id) + ".toml");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw DevicePresetError("device preset is unavailable: " + std::string(id));
    std::string text;
    text.resize(kMaximumBytes + 1U);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(input.gcount()));
    try {
        return LoadDevicePresetText(text, id);
    } catch (const DevicePresetError& error) {
        throw DevicePresetError(std::string(id) + ".toml: " + error.what());
    } catch (const std::exception& error) {  // exact-key failures from the shared TOML helper
        throw DevicePresetError(std::string(id) + ".toml: " + error.what());
    }
}

}  // namespace ogplay::session
