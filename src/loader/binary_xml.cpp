// Generic Android binary XML (AXML) start-element walker used for layout
// inflation. Format reference: AOSP ResourceTypes.h (ResXMLTree chunks) at
// the pinned baseline; apk_manifest.cpp holds the manifest-specific reader.

#include "ogplay/loader/binary_xml.h"

#include <stdexcept>
#include <string_view>

#include "ogplay/core/byte_order.h"
#include "ogplay/core/text.h"

namespace ogplay::loader {
namespace {

constexpr std::uint16_t kXmlType = 0x0003;
constexpr std::uint16_t kStringPoolType = 0x0001;
constexpr std::uint16_t kStartElementType = 0x0102;
constexpr std::uint16_t kEndElementType = 0x0103;
constexpr std::uint16_t kCdataType = 0x0104;
constexpr std::uint32_t kNoIndex = 0xffffffffU;
// Res_value data types (AOSP ResourceTypes.h).
constexpr std::uint8_t kTypeDimension = 0x05;
constexpr std::uint8_t kTypeString = 0x03;
constexpr std::uint32_t kUtf8Flag = 0x00000100U;
constexpr std::string_view kAndroidNamespace =
    "http://schemas.android.com/apk/res/android";

void RequireRange(const std::span<const std::byte> bytes,
                  const std::size_t offset, const std::size_t size) {
    if (!core::RangeFits(bytes, offset, size)) {
        throw std::runtime_error("binary XML field is out of range");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes,
                     const std::size_t offset) {
    RequireRange(bytes, offset, 2);
    return core::ReadLittleEndian<std::uint16_t>(bytes, offset);
}

std::uint32_t Read32(const std::span<const std::byte> bytes,
                     const std::size_t offset) {
    RequireRange(bytes, offset, 4);
    return core::ReadLittleEndian<std::uint32_t>(bytes, offset);
}

std::size_t ReadLength8(const std::span<const std::byte> bytes, std::size_t& cursor, const std::size_t end) {
    if (cursor >= end) throw std::runtime_error("binary XML UTF-8 length is truncated");
    const auto first = std::to_integer<std::uint8_t>(bytes[cursor++]);
    if ((first & 0x80U) == 0) return first;
    if (cursor >= end) throw std::runtime_error("binary XML UTF-8 length is truncated");
    return (static_cast<std::size_t>(first & 0x7fU) << 8U) | std::to_integer<std::uint8_t>(bytes[cursor++]);
}

std::size_t ReadLength16(const std::span<const std::byte> bytes, std::size_t& cursor, const std::size_t end) {
    if (cursor > end || end - cursor < 2) {
        throw std::runtime_error("binary XML UTF-16 length is truncated");
    }
    const auto first = Read16(bytes, cursor);
    cursor += 2;
    if ((first & 0x8000U) == 0) return first;
    if (cursor > end || end - cursor < 2) {
        throw std::runtime_error("binary XML UTF-16 length is truncated");
    }
    const auto second = Read16(bytes, cursor);
    cursor += 2;
    return (static_cast<std::size_t>(first & 0x7fffU) << 16U) | second;
}

// Preserve Java UTF-16 code units, including isolated surrogates, in WTF-8.
void AppendStringUnit(std::string& output, const std::uint16_t unit) {
    if (unit >= 0xd800U && unit <= 0xdfffU) {
        output.push_back(static_cast<char>(0xe0U | (unit >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((unit >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (unit & 0x3fU)));
    } else {
        static_cast<void>(core::AppendUtf8(output, unit));
    }
}

std::string ReadUtf8String(const std::span<const std::byte> bytes, std::size_t cursor, const std::size_t end) {
    const auto expected_utf16 = ReadLength8(bytes, cursor, end);
    const auto byte_length = ReadLength8(bytes, cursor, end);
    if (cursor > end || byte_length >= end - cursor) {
        throw std::runtime_error("binary XML UTF-8 string is truncated");
    }
    std::string result;
    std::size_t units{};
    for (std::size_t index = 0; index < byte_length;) {
        const auto first = std::to_integer<std::uint8_t>(bytes[cursor + index]);
        // API19 libutils/Unicode.cpp: utf8_codepoint_len and mask conversion.
        // It does not require canonical encodings or validate continuation tags.
        const auto count = static_cast<std::size_t>(((0xe5000000U >> ((first >> 3U) & 0x1eU)) & 3U) + 1U);
        if (count > byte_length - index) throw std::runtime_error("binary XML string pool contains truncated UTF-8");
        std::uint32_t code = count == 1 ? first : first & (0x7fU >> count);
        for (std::size_t i = 1; i < count; ++i)
            code = (code << 6U) | (std::to_integer<std::uint8_t>(bytes[cursor + index + i]) & 0x3fU);
        if (code > 0xffffU) {
            code -= 0x10000U;
            AppendStringUnit(result, static_cast<std::uint16_t>((code >> 10U) + 0xd800U));
            AppendStringUnit(result, static_cast<std::uint16_t>((code & 0x3ffU) + 0xdc00U));
            units += 2;
        } else {
            AppendStringUnit(result, static_cast<std::uint16_t>(code));
            ++units;
        }
        index += count;
    }
    if (units != expected_utf16) throw std::runtime_error("binary XML UTF-8 string length disagrees with content");
    return result;
}

std::string ReadUtf16String(const std::span<const std::byte> bytes, std::size_t cursor, const std::size_t end) {
    const auto length = ReadLength16(bytes, cursor, end);
    if (length > (end - cursor) / 2U || length == (end - cursor) / 2U) {
        throw std::runtime_error("binary XML UTF-16 string is truncated");
    }
    std::vector<std::uint16_t> units;
    units.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        const auto unit = Read16(bytes, cursor + index * 2U);
        units.push_back(unit);
    }
    std::string result;
    for (const auto unit : units)
        AppendStringUnit(result, unit);
    return result;
}

struct StringPool final {
    std::span<const std::byte> bytes;
    std::vector<std::size_t> offsets;
    std::size_t end{};
    bool utf8{};

    [[nodiscard]] std::optional<std::string> Get(std::uint32_t index) const {
        if (index >= offsets.size()) return std::nullopt;
        try {
            return utf8 ? ReadUtf8String(bytes, offsets[index], end) : ReadUtf16String(bytes, offsets[index], end);
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
    }

    [[nodiscard]] std::string At(std::uint32_t index) const { return Get(index).value_or(std::string{}); }
};

StringPool ParseStringPool(std::span<const std::byte> bytes, std::size_t offset, std::uint32_t size) {
    RequireRange(bytes, offset, size);
    if (size < 28) throw std::runtime_error("binary XML string pool is truncated");
    StringPool pool;
    pool.bytes = bytes;
    pool.end = offset + size;
    pool.utf8 = (Read32(bytes, offset + 16) & kUtf8Flag) != 0;
    const auto count = Read32(bytes, offset + 8);
    const auto header = Read16(bytes, offset + 2);
    const auto start = Read32(bytes, offset + 20);
    if (header > size || count > (size - header) / 4U || start > size)
        throw std::runtime_error("binary XML string pool field is out of range");
    for (std::uint32_t i = 0; i < count; ++i)
        pool.offsets.push_back(offset + start + Read32(bytes, offset + header + i * 4U));
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

struct ParsedBinaryXml final {
    std::vector<BinaryXmlElement> elements;
    std::vector<BinaryXmlPullEvent> events;
};

ParsedBinaryXml ParseBinaryXml(const std::span<const std::byte> bytes) {
    if (Read16(bytes, 0) != kXmlType) {
        throw std::runtime_error("not a binary XML document");
    }
    const auto document_size = Read32(bytes, 4);
    RequireRange(bytes, 0, document_size);

    StringPool pool;
    bool saw_pool = false;
    std::string pool_error;
    ParsedBinaryXml result;
    result.events.push_back(
        {BinaryXmlPullEventType::start_document, {}, {}, 0});
    std::vector<std::int32_t> open;  // element indices on the tag stack
    std::int32_t depth{};
    bool saw_node = false;
    std::size_t offset = Read16(bytes, 2);
    if (offset < 8 || offset > document_size) throw std::runtime_error("binary XML header is malformed");
    while (offset + 8 <= document_size) {
        const auto type = Read16(bytes, offset);
        const auto header_size = Read16(bytes, offset + 2);
        const auto size = Read32(bytes, offset + 4);
        if (size < header_size || header_size < 8) {
            throw std::runtime_error("binary XML chunk is malformed");
        }
        RequireRange(bytes.first(document_size), offset, size);
        if (type == kStringPoolType) {
            if (!saw_node) {
                try {
                    pool = ParseStringPool(bytes, offset, size);
                    saw_pool = true;
                    pool_error.clear();
                } catch (const std::runtime_error& error) {
                    pool = {};
                    saw_pool = false;
                    pool_error = error.what();
                }
            }
        } else if (type == kStartElementType) {
            if (!saw_pool) {
                throw std::runtime_error(pool_error.empty() ? "binary XML has no string pool" : pool_error);
            }
            saw_node = true;
            if (size - header_size < 20) throw std::runtime_error("binary XML start chunk is malformed");
            // ResXMLTree_attrExt follows the declared node header.
            const auto ext = offset + header_size;
            BinaryXmlElement element;
            element.name = pool.At(Read32(bytes, ext + 4));
            element.parent = open.empty() ? -1 : open.back();
            const auto event_name = element.name;
            const auto attribute_start = Read16(bytes, ext + 8);
            const auto attribute_size = Read16(bytes, ext + 10);
            const auto attribute_count = Read16(bytes, ext + 12);
            if (attribute_count != 0 && attribute_size < 20) {
                throw std::runtime_error("binary XML attribute is malformed");
            }
            RequireRange(bytes.first(offset + size), ext + attribute_start,
                         static_cast<std::size_t>(attribute_size) * attribute_count);
            for (std::uint16_t index = 0; index < attribute_count;
                 ++index) {
                const auto attr =
                    ext + attribute_start +
                    static_cast<std::size_t>(attribute_size) * index;
                const auto ns = pool.At(Read32(bytes, attr));
                const auto name = pool.At(Read32(bytes, attr + 4));
                const auto raw_index = Read32(bytes, attr + 8);
                RequireRange(bytes, attr + 15, 5);
                const auto value_type =
                    std::to_integer<std::uint8_t>(bytes[attr + 15]);
                const auto data = Read32(bytes, attr + 16);
                BinaryXmlAttribute attribute{
                    .namespace_uri = ns,
                    .name = name,
                    .value_type = value_type,
                    .data = data,
                };
                if (raw_index != kNoIndex) {
                    if (raw_index >= pool.offsets.size())
                        throw std::runtime_error("binary XML raw attribute string is invalid");
                    attribute.raw_string = pool.Get(raw_index);
                } else if (value_type == kTypeString) {
                    if (data >= pool.offsets.size()) throw std::runtime_error("binary XML typed string is invalid");
                    attribute.raw_string = pool.Get(data);
                }
                element.attributes.push_back(std::move(attribute));
                if (ns != kAndroidNamespace) continue;
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
            open.push_back(static_cast<std::int32_t>(result.elements.size()));
            result.elements.push_back(std::move(element));
            result.events.push_back({BinaryXmlPullEventType::start_tag, event_name, {}, ++depth});
        } else if (type == kEndElementType) {
            saw_node = true;
            if (size - header_size < 8) throw std::runtime_error("binary XML end chunk is malformed");
            const auto ext = offset + header_size;
            const auto closing_name = pool.At(Read32(bytes, ext + 4));
            result.events.push_back({BinaryXmlPullEventType::end_tag, closing_name, {}, depth--});
            if (!open.empty()) open.pop_back();
        } else if (type == kCdataType) {
            saw_node = true;
            if (size - header_size < 12U) {
                throw std::runtime_error("binary XML text chunk is malformed");
            }
            const auto ext = offset + header_size;
            RequireRange(bytes, ext, 12);
            const auto text_index = Read32(bytes, ext);
            if (!saw_pool || text_index >= pool.offsets.size())
                throw std::runtime_error("binary XML text string is invalid");
            result.events.push_back({BinaryXmlPullEventType::text, {}, pool.At(text_index), depth});
        }
        if (type >= 0x0100 && type <= 0x017f) {
            if (!saw_pool) throw std::runtime_error(pool_error.empty() ? "binary XML has no string pool" : pool_error);
            saw_node = true;
        }
        offset += size;
    }
    if (offset != document_size) {
        throw std::runtime_error("binary XML document has trailing bytes");
    }
    result.events.push_back({BinaryXmlPullEventType::end_document, {}, {}, depth});
    return result;
}

}  // namespace

std::vector<BinaryXmlElement> ParseBinaryXmlElements(
    const std::span<const std::byte> bytes) {
    return ParseBinaryXml(bytes).elements;
}

std::vector<BinaryXmlPullEvent> ParseBinaryXmlPullEvents(
    const std::span<const std::byte> bytes) {
    return ParseBinaryXml(bytes).events;
}

}  // namespace ogplay::loader
