#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ogplay/loader/dex.h"

namespace ogplay::loader {

// Strict resources.arsc (ResTable) reader for the facts legacy titles
// actually consume: resource id -> (type, entry name, file path string) and
// (type, entry name) -> resource id. Complex/styled values, locales other
// than the default configuration and attribute resolution are out of scope;
// malformed chunks fail loudly (same posture as ZIP/Manifest/DEX parsing).

struct ArscEntry final {
    std::uint32_t resource_id{};
    std::string type_name;   // "raw", "drawable", ...
    std::string entry_name;  // "raw_000", "icon", ...
    // TYPE_STRING values carry the file path inside the APK.
    std::optional<std::string> string_value;
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
