#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ogplay::loader {

struct BinaryXmlAttribute final {
    std::string namespace_uri;
    std::string name;
    std::uint8_t value_type{};
    std::uint32_t data{};
    std::optional<std::string> raw_string;
};

// One start-element of an Android binary XML document in document order.
// attributes is the generic, lossless typed output. The named layout fields
// below are a temporary compatibility adapter for callers that predate it;
// new widget semantics must interpret attributes outside the loader.
struct BinaryXmlElement final {
    std::string name;
    std::vector<BinaryXmlAttribute> attributes;
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
