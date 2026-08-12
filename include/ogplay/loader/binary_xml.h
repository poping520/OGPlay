#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ogplay::loader {

// One start-element of an Android binary XML document (layout inflation
// subset): the tag name, the android:id reference, the element's parent in
// the flat vector, plus the layout attributes the widget layer derives
// bounds from. Dimensions resolve complex values by mantissa only (dp is
// treated as px at density 1, a recorded approximation).
struct BinaryXmlElement final {
    std::string name;
    std::uint32_t id{};        // android:id resource reference, 0 when absent
    std::int32_t parent{-1};   // index of the enclosing element, -1 for roots

    static constexpr std::int32_t kSizeUnspecified = 0;
    static constexpr std::int32_t kSizeFillParent = -1;
    static constexpr std::int32_t kSizeWrapContent = -2;
    std::int32_t layout_width{kSizeUnspecified};
    std::int32_t layout_height{kSizeUnspecified};

    std::uint32_t gravity{};         // android:gravity, 0 when absent
    std::uint32_t layout_gravity{};  // android:layout_gravity, 0 when absent
    std::int32_t padding_top{};      // android:paddingTop in px, 0 when absent
    std::uint32_t src{};             // android:src resource ref, 0 when absent
};

// Flat document order (parents before children). Throws std::runtime_error
// on malformed input.
[[nodiscard]] std::vector<BinaryXmlElement> ParseBinaryXmlElements(
    std::span<const std::byte> bytes);

}  // namespace ogplay::loader
