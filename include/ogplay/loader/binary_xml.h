#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ogplay::loader {

// One start-element of an Android binary XML document (layout inflation
// subset): the tag name plus the android:id reference when present.
struct BinaryXmlElement final {
    std::string name;
    std::uint32_t id{};  // android:id resource reference, 0 when absent
};

// Flat document order (parents before children). Throws std::runtime_error
// on malformed input.
[[nodiscard]] std::vector<BinaryXmlElement> ParseBinaryXmlElements(
    std::span<const std::byte> bytes);

}  // namespace ogplay::loader
