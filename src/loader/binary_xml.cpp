// Generic Android binary XML (AXML) start-element walker used for layout
// inflation. Format reference: AOSP ResourceTypes.h (ResXMLTree chunks) at
// the pinned baseline; apk_manifest.cpp holds the manifest-specific reader.

#include "ogplay/loader/binary_xml.h"

#include <stdexcept>
#include <string_view>

namespace ogplay::loader {
namespace {

constexpr std::uint16_t kXmlType = 0x0003;
constexpr std::uint16_t kStringPoolType = 0x0001;
constexpr std::uint16_t kStartElementType = 0x0102;
constexpr std::uint16_t kEndElementType = 0x0103;
constexpr std::uint32_t kNoIndex = 0xffffffffU;
// Res_value data types (AOSP ResourceTypes.h).
constexpr std::uint8_t kTypeDimension = 0x05;
constexpr std::uint32_t kUtf8Flag = 0x00000100U;
constexpr std::string_view kAndroidNamespace =
    "http://schemas.android.com/apk/res/android";

void RequireRange(const std::span<const std::byte> bytes,
                  const std::size_t offset, const std::size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw std::runtime_error("binary XML field is out of range");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes,
                     const std::size_t offset) {
    RequireRange(bytes, offset, 2);
    return static_cast<std::uint16_t>(
               std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(
                   std::to_integer<std::uint8_t>(bytes[offset + 1]))
               << 8U);
}

std::uint32_t Read32(const std::span<const std::byte> bytes,
                     const std::size_t offset) {
    RequireRange(bytes, offset, 4);
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

struct StringPool final {
    std::vector<std::string> values;

    [[nodiscard]] std::string At(const std::uint32_t index) const {
        if (index == kNoIndex || index >= values.size()) return {};
        return values[index];
    }
};

StringPool ParseStringPool(const std::span<const std::byte> bytes,
                           const std::size_t offset,
                           const std::uint32_t chunk_size) {
    StringPool pool;
    const auto count = Read32(bytes, offset + 8);
    const auto flags = Read32(bytes, offset + 16);
    const auto strings_start = Read32(bytes, offset + 20);
    const bool utf8 = (flags & kUtf8Flag) != 0;
    const auto base = offset + strings_start;
    RequireRange(bytes, offset, chunk_size);
    pool.values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto entry_offset =
            Read32(bytes, offset + 28 + 4U * index);
        auto cursor = base + entry_offset;
        std::string value;
        if (utf8) {
            RequireRange(bytes, cursor, 2);
            // UTF-16 length prefix (skipped), then UTF-8 byte length.
            auto skip = std::to_integer<std::uint8_t>(bytes[cursor++]);
            if ((skip & 0x80U) != 0) ++cursor;
            RequireRange(bytes, cursor, 1);
            std::size_t length =
                std::to_integer<std::uint8_t>(bytes[cursor++]);
            if ((length & 0x80U) != 0) {
                RequireRange(bytes, cursor, 1);
                length = ((length & 0x7fU) << 8U) |
                         std::to_integer<std::uint8_t>(bytes[cursor++]);
            }
            RequireRange(bytes, cursor, length);
            value.assign(
                reinterpret_cast<const char*>(bytes.data()) + cursor,
                length);
        } else {
            std::size_t length = Read16(bytes, cursor);
            cursor += 2;
            if ((length & 0x8000U) != 0) {
                length = ((length & 0x7fffU) << 16U) | Read16(bytes, cursor);
                cursor += 2;
            }
            RequireRange(bytes, cursor, length * 2);
            // ASCII-compatible narrow conversion; non-BMP layout names do
            // not occur in practice and non-ASCII units degrade to '?'.
            value.reserve(length);
            for (std::size_t unit = 0; unit < length; ++unit) {
                const auto code = Read16(bytes, cursor + unit * 2);
                value.push_back(code < 0x80U ? static_cast<char>(code)
                                             : '?');
            }
        }
        pool.values.push_back(std::move(value));
    }
    return pool;
}

// Complex dimension (TYPE_DIMENSION) reduced to its integer mantissa; dp
// and px share the density-1 interpretation, other radixes truncate.
std::int32_t DimensionPx(const std::uint32_t data) {
    return static_cast<std::int32_t>(data) >> 8;
}

// Layout size attribute: fill_parent/wrap_content arrive as INT_DEC
// -1/-2, explicit sizes as dimensions.
std::int32_t LayoutSize(const std::uint8_t type, const std::uint32_t data) {
    if (type == kTypeDimension) return DimensionPx(data);
    return static_cast<std::int32_t>(data);
}

}  // namespace

std::vector<BinaryXmlElement> ParseBinaryXmlElements(
    const std::span<const std::byte> bytes) {
    if (Read16(bytes, 0) != kXmlType) {
        throw std::runtime_error("not a binary XML document");
    }
    const auto document_size = Read32(bytes, 4);
    RequireRange(bytes, 0, document_size);

    StringPool pool;
    std::vector<BinaryXmlElement> elements;
    std::vector<std::int32_t> open;  // element indices on the tag stack
    std::size_t offset = 8;
    while (offset + 8 <= document_size) {
        const auto type = Read16(bytes, offset);
        const auto header_size = Read16(bytes, offset + 2);
        const auto size = Read32(bytes, offset + 4);
        if (size < header_size || size < 8) {
            throw std::runtime_error("binary XML chunk is malformed");
        }
        RequireRange(bytes, offset, size);
        if (type == kStringPoolType) {
            pool = ParseStringPool(bytes, offset, size);
        } else if (type == kStartElementType) {
            // ResXMLTree_attrExt after the 16-byte node header.
            const auto ext = offset + header_size;
            BinaryXmlElement element;
            element.name = pool.At(Read32(bytes, ext + 4));
            element.parent = open.empty() ? -1 : open.back();
            const auto attribute_start = Read16(bytes, ext + 8);
            const auto attribute_size = Read16(bytes, ext + 10);
            const auto attribute_count = Read16(bytes, ext + 12);
            for (std::uint16_t index = 0; index < attribute_count;
                 ++index) {
                const auto attr =
                    ext + attribute_start +
                    static_cast<std::size_t>(attribute_size) * index;
                const auto ns = pool.At(Read32(bytes, attr));
                if (ns != kAndroidNamespace) continue;
                const auto name = pool.At(Read32(bytes, attr + 4));
                RequireRange(bytes, attr + 15, 5);
                const auto value_type =
                    std::to_integer<std::uint8_t>(bytes[attr + 15]);
                const auto data = Read32(bytes, attr + 16);
                if (name == "id") {
                    // Typed value data (reference kind) is the resource id.
                    element.id = data;
                } else if (name == "layout_width") {
                    element.layout_width = LayoutSize(value_type, data);
                } else if (name == "layout_height") {
                    element.layout_height = LayoutSize(value_type, data);
                } else if (name == "gravity") {
                    element.gravity = data;
                } else if (name == "layout_gravity") {
                    element.layout_gravity = data;
                } else if (name == "paddingTop") {
                    element.padding_top = DimensionPx(data);
                } else if (name == "src") {
                    element.src = data;
                }
            }
            open.push_back(static_cast<std::int32_t>(elements.size()));
            elements.push_back(std::move(element));
        } else if (type == kEndElementType) {
            if (!open.empty()) open.pop_back();
        }
        offset += size;
    }
    return elements;
}

}  // namespace ogplay::loader
