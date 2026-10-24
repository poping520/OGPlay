#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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

enum class BinaryXmlPullEventType : std::int32_t {
    start_document = 0,
    end_document = 1,
    start_tag = 2,
    end_tag = 3,
    text = 4,
};

// High-level pull events preserved by Android's compiled XML format. Comments,
// entities and other source-only tokens have already been removed by aapt.
struct BinaryXmlPullEvent final {
    BinaryXmlPullEventType type{};
    std::string name;
    std::string text;
    std::int32_t depth{};
};

// Raw pool identities are never inferred from attribute names. kNoIndex means
// absent; in particular raw_value == kNoIndex differs from a pooled empty string.
inline constexpr std::uint32_t kBinaryXmlNoIndex = 0xffffffffU;

struct CompiledXmlValue final {
    std::uint16_t size{};
    std::uint8_t reserved{}, type{};
    std::uint32_t data{};
};

struct CompiledXmlAttribute final {
    std::uint32_t namespace_index{}, name_index{}, raw_value_index{};
    CompiledXmlValue value;
    std::uint32_t resource_id{};
};

struct CompiledXmlNode final {
    std::uint16_t type{};  // ResXMLTree chunk type, including namespace nodes
    std::uint32_t line{}, comment_index{}, namespace_index{kBinaryXmlNoIndex};
    std::uint32_t name_index{kBinaryXmlNoIndex}, text_index{kBinaryXmlNoIndex};
    CompiledXmlValue text_value;
    std::vector<CompiledXmlAttribute> attributes;
    std::uint16_t id_index{}, class_index{}, style_index{};  // original one-based indices
    std::optional<std::size_t> matching_node;
    std::int32_t parent_node{-1}, depth{};
};

// Owns the original bytes and decoded facts. Published only through const
// ownership; no resource resolution, attribute semantics or layout defaults.
class CompiledXmlDocument final {
public:
    [[nodiscard]] std::span<const std::byte> Bytes() const { return bytes_; }

    [[nodiscard]] std::span<const std::uint32_t> ResourceMap() const { return resource_map_; }

    [[nodiscard]] std::span<const CompiledXmlNode> Nodes() const { return nodes_; }

    [[nodiscard]] bool HasStringStyles() const { return has_string_styles_; }

    [[nodiscard]] std::size_t StringCount() const { return string_offsets_.size(); }

    // Missing index, invalid index and invalid encoding are distinct. Legacy
    // adapters alone retain the historical failed-conversion fallback.
    [[nodiscard]] std::optional<std::string> String(std::uint32_t index) const;
    [[nodiscard]] std::vector<BinaryXmlElement> Elements() const;
    [[nodiscard]] std::vector<BinaryXmlPullEvent> PullEvents() const;

private:
    friend struct CompiledXmlDocumentBuilder;
    std::vector<std::byte> bytes_;
    std::vector<std::size_t> string_offsets_;
    std::size_t string_end_{};
    bool utf8_{}, has_string_styles_{};
    std::vector<std::uint32_t> resource_map_;
    std::vector<CompiledXmlNode> nodes_;
};

// Strict fact extraction rejects unsupported chunks, invalid pool/special
// indices and unpaired tags. Legacy adapters use the same decoder with their
// existing permissive structural policy.
[[nodiscard]] std::shared_ptr<const CompiledXmlDocument> ParseCompiledXmlDocument(std::span<const std::byte> bytes);

[[nodiscard]] std::shared_ptr<const CompiledXmlDocument> ParseCompiledStringPool(std::span<const std::byte> bytes);

// Flat document order (parents before children). Throws std::runtime_error
// on malformed input.
[[nodiscard]] std::vector<BinaryXmlElement> ParseBinaryXmlElements(
    std::span<const std::byte> bytes);
[[nodiscard]] std::vector<BinaryXmlPullEvent> ParseBinaryXmlPullEvents(
    std::span<const std::byte> bytes);

}  // namespace ogplay::loader
