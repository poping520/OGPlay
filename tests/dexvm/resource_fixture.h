#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>
#include <span>

namespace ogplay::runtime {
struct DexVmAndroidContext;
}

namespace ogplay::test {
struct ResourceFixtureBagValue final {
    std::uint32_t name{};
    std::uint8_t value_type{};
    std::uint32_t value_data{};
    std::optional<std::string> string_value;
};

struct ResourceFixtureEntry final {
    std::uint32_t resource_id{};
    std::string type_name;   // "raw", "drawable", ...
    std::string entry_name;  // "raw_000", "icon", ...
    // TYPE_STRING values carry the file path inside the APK.
    std::optional<std::string> string_value;
    // Simple Res_value is retained losslessly for UI string/color/dimension
    // and reference resolution. Bag entries preserve the same typed values.
    std::uint8_t value_type{};
    std::uint32_t value_data{};
    bool is_complex{};
    std::uint32_t parent{};
    std::vector<ResourceFixtureBagValue> bag;
    std::uint32_t changing_configurations{};
    // BCP 47 style "zh", "zh-TW"; empty for the default configuration.
    std::string locale;
    std::uint8_t orientation{};
    std::uint16_t density{};
    std::uint16_t sdk_version{};
    std::uint8_t screen_layout{};
    std::uint16_t smallest_width_dp{};
    std::uint16_t screen_width_dp{};
    std::uint16_t screen_height_dp{};
};

struct ResourceFixtureTable final {
    std::string package_name;
    std::uint32_t package_id{};
    std::vector<ResourceFixtureEntry> entries;
    // Original global pool chunk, including style metadata and pool indices.
    std::vector<std::byte> global_string_pool;
};

ResourceFixtureTable ReadResourceFixture(std::span<const std::byte>);
ResourceFixtureTable& ResourceFixture(runtime::DexVmAndroidContext&);
}  // namespace ogplay::test
