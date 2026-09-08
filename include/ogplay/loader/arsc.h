#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/loader/dex.h"

namespace ogplay::loader {

// Strict resources.arsc (ResTable) reader for the facts legacy titles
// actually consume: resource id -> (type, entry name, typed simple value) and
// (type, entry name) -> resource id. Bag parent/items are retained; locales other
// than the default configuration and attribute resolution belong to the caller;
// malformed chunks fail loudly (same posture as ZIP/Manifest/DEX parsing).

struct ArscBagValue final {
    std::uint32_t name{};
    std::uint8_t value_type{};
    std::uint32_t value_data{};
    std::optional<std::string> string_value;
};

struct ArscEntry final {
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
    std::vector<ArscBagValue> bag;
};

struct ArscTable final {
    std::string package_name;
    std::uint32_t package_id{};
    std::vector<ArscEntry> entries;

    [[nodiscard]] const ArscEntry* FindById(
        std::uint32_t resource_id) const noexcept;
    [[nodiscard]] const ArscEntry* FindByName(
        std::string_view type_name,
        std::string_view entry_name) const noexcept;
};

[[nodiscard]] ArscTable ParseArsc(std::span<const std::uint8_t> bytes);

}  // namespace ogplay::loader
