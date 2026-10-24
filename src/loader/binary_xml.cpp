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
    std::vector<std::size_t> offsets;
    std::size_t end{};
    bool utf8{};
};

StringPool ParseStringPool(std::span<const std::byte> bytes, std::size_t offset, std::uint32_t size) {
    RequireRange(bytes, offset, size);
    if (size < 28) throw std::runtime_error("binary XML string pool is truncated");
    StringPool pool;
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

}  // namespace

struct CompiledXmlDocumentBuilder final {
    static std::shared_ptr<const CompiledXmlDocument> Parse(const std::span<const std::byte> input, bool strict) {
        auto result = std::make_shared<CompiledXmlDocument>();
        result->bytes_.assign(input.begin(), input.end());
        const std::span<const std::byte> bytes(result->bytes_);
        if (Read16(bytes, 0) != kXmlType) throw std::runtime_error("not a binary XML document");
        const auto document_size = Read32(bytes, 4);
        RequireRange(bytes, 0, document_size);
        StringPool pool;
        bool saw_pool = false, saw_node = false;
        std::string pool_error;
        std::vector<std::size_t> open, namespaces;
        std::int32_t depth{};
        auto check_string = [&](std::uint32_t index, bool optional) {
            if (strict && !(optional && index == kNoIndex) && index >= pool.offsets.size())
                throw std::runtime_error("binary XML pool index is invalid");
        };
        auto value = [&](std::size_t offset) {
            return CompiledXmlValue{Read16(bytes, offset), std::to_integer<std::uint8_t>(bytes[offset + 2]),
                                    std::to_integer<std::uint8_t>(bytes[offset + 3]), Read32(bytes, offset + 4)};
        };
        std::size_t offset = Read16(bytes, 2);
        if (offset < 8 || offset > document_size) throw std::runtime_error("binary XML header is malformed");
        while (offset + 8 <= document_size) {
            const auto type = Read16(bytes, offset);
            const auto header = Read16(bytes, offset + 2);
            const auto size = Read32(bytes, offset + 4);
            if (size < header || header < 8) throw std::runtime_error("binary XML chunk is malformed");
            RequireRange(bytes.first(document_size), offset, size);
            const auto ext = offset + header;
            if (type == kStringPoolType && !saw_node) {
                try {
                    pool = ParseStringPool(bytes, offset, size);
                    saw_pool = true;
                    pool_error.clear();
                } catch (const std::runtime_error& error) {
                    pool = {};
                    saw_pool = false;
                    pool_error = error.what();
                }
            } else if (type == 0x0180 && !saw_node) {
                if (strict && (size - header) % 4 != 0)
                    throw std::runtime_error("binary XML resource map is malformed");
                result->resource_map_.clear();
                for (std::size_t i = ext; i + 4 <= offset + size; i += 4)
                    result->resource_map_.push_back(Read32(bytes, i));
            } else if (type >= 0x0100 && type <= 0x017f) {
                if (!saw_pool)
                    throw std::runtime_error(pool_error.empty() ? "binary XML has no string pool" : pool_error);
                saw_node = true;
                if (strict && header < 16) throw std::runtime_error("binary XML node header is malformed");
                CompiledXmlNode node;
                node.type = type;
                // Legacy accepted short headers; never read line/comment outside the chunk.
                if (header >= 16) {
                    node.line = Read32(bytes, offset + 8);
                    node.comment_index = Read32(bytes, offset + 12);
                } else
                    node.comment_index = kNoIndex;
                check_string(node.comment_index, true);
                node.depth = depth;
                if (type == kStartElementType) {
                    if (size - header < 20) throw std::runtime_error("binary XML start chunk is malformed");
                    node.namespace_index = Read32(bytes, ext);
                    node.name_index = Read32(bytes, ext + 4);
                    check_string(node.namespace_index, true);
                    check_string(node.name_index, false);

                    const auto start = Read16(bytes, ext + 8), stride = Read16(bytes, ext + 10),
                               count = Read16(bytes, ext + 12);
                    node.id_index = Read16(bytes, ext + 14);
                    node.class_index = Read16(bytes, ext + 16);
                    node.style_index = Read16(bytes, ext + 18);
                    if (strict && (node.id_index > count || node.class_index > count || node.style_index > count))
                        throw std::runtime_error("binary XML special attribute index is invalid");
                    if (count != 0 && stride < 20) throw std::runtime_error("binary XML attribute is malformed");
                    if (strict && start < 20) throw std::runtime_error("binary XML attribute start is invalid");
                    RequireRange(bytes.first(offset + size), ext + start, static_cast<std::size_t>(stride) * count);
                    for (std::uint16_t i = 0; i < count; ++i) {
                        const auto attr = ext + start + static_cast<std::size_t>(stride) * i;
                        CompiledXmlAttribute fact{Read32(bytes, attr), Read32(bytes, attr + 4), Read32(bytes, attr + 8),
                                                  value(attr + 12)};
                        check_string(fact.namespace_index, true);
                        check_string(fact.name_index, false);
                        check_string(fact.raw_value_index, true);
                        if (fact.raw_value_index != kNoIndex && fact.raw_value_index >= pool.offsets.size())
                            throw std::runtime_error("binary XML raw attribute string is invalid");
                        if (fact.value.type == kTypeString && (strict || fact.raw_value_index == kNoIndex)) {
                            if (fact.value.data >= pool.offsets.size())
                                throw std::runtime_error("binary XML typed string is invalid");
                        }
                        if (fact.name_index < result->resource_map_.size())
                            fact.resource_id = result->resource_map_[fact.name_index];
                        node.attributes.push_back(fact);
                    }
                    node.depth = ++depth;
                    node.parent_node = open.empty() ? -1 : static_cast<std::int32_t>(open.back());
                    open.push_back(result->nodes_.size());
                } else if (type == kEndElementType) {
                    if (size - header < 8) throw std::runtime_error("binary XML end chunk is malformed");
                    node.namespace_index = Read32(bytes, ext);
                    node.name_index = Read32(bytes, ext + 4);
                    check_string(node.namespace_index, true);
                    check_string(node.name_index, false);
                    node.depth = depth--;
                    if (!open.empty()) {
                        auto& start = result->nodes_[open.back()];
                        if (strict &&
                            (start.name_index != node.name_index || start.namespace_index != node.namespace_index))
                            throw std::runtime_error("binary XML end tag does not match start");
                        node.matching_node = open.back();
                        start.matching_node = result->nodes_.size();
                        open.pop_back();
                    } else if (strict)
                        throw std::runtime_error("binary XML end tag has no start");
                } else if (type == kCdataType) {
                    if (size - header < 12) throw std::runtime_error("binary XML text chunk is malformed");
                    node.text_index = Read32(bytes, ext);
                    node.text_value = value(ext + 4);
                    if (node.text_index >= pool.offsets.size())
                        throw std::runtime_error("binary XML text string is invalid");
                    if (strict && node.text_value.type == kTypeString) check_string(node.text_value.data, false);
                } else if ((type == 0x0100 || type == 0x0101) && strict) {
                    if (size - header < 8) throw std::runtime_error("binary XML namespace chunk is malformed");
                    node.name_index = Read32(bytes, ext);
                    node.namespace_index = Read32(bytes, ext + 4);
                    check_string(node.name_index, true);
                    check_string(node.namespace_index, false);
                    if (type == 0x0100)
                        namespaces.push_back(result->nodes_.size());
                    else if (!namespaces.empty()) {
                        auto& start = result->nodes_[namespaces.back()];
                        if (strict &&
                            (start.name_index != node.name_index || start.namespace_index != node.namespace_index))
                            throw std::runtime_error("binary XML namespace end does not match start");
                        node.matching_node = namespaces.back();
                        start.matching_node = result->nodes_.size();
                        namespaces.pop_back();
                    } else if (strict)
                        throw std::runtime_error("binary XML namespace end has no start");
                } else if (strict)
                    throw std::runtime_error("binary XML node type is unsupported");
                result->nodes_.push_back(std::move(node));
            } else if (strict)
                throw std::runtime_error("binary XML chunk type or position is unsupported");
            offset += size;
        }
        if (offset != document_size) throw std::runtime_error("binary XML document has trailing bytes");
        if (strict && (!saw_node || !open.empty() || !namespaces.empty()))
            throw std::runtime_error("binary XML document has unclosed tags or no nodes");
        result->string_offsets_ = std::move(pool.offsets);
        result->string_end_ = pool.end;
        result->utf8_ = pool.utf8;
        return result;
    }
};

std::optional<std::string> CompiledXmlDocument::String(std::uint32_t index) const {
    if (index == kNoIndex) return std::nullopt;
    if (index >= string_offsets_.size()) throw std::out_of_range("binary XML pool index is invalid");
    return utf8_ ? ReadUtf8String(bytes_, string_offsets_[index], string_end_)
                 : ReadUtf16String(bytes_, string_offsets_[index], string_end_);
}

namespace {
std::optional<std::string> LegacyString(const CompiledXmlDocument& doc, std::uint32_t index) {
    try {
        return doc.String(index);
    } catch (const std::out_of_range&) {
        return std::nullopt;
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

std::string LegacyName(const CompiledXmlDocument& doc, std::uint32_t index) {
    return LegacyString(doc, index).value_or(std::string{});
}
}  // namespace

std::vector<BinaryXmlElement> CompiledXmlDocument::Elements() const {
    std::vector<BinaryXmlElement> result;
    std::vector<std::int32_t> open;
    for (const auto& node : nodes_) {
        if (node.type == kEndElementType) {
            if (!open.empty()) open.pop_back();
            continue;
        }
        if (node.type != kStartElementType) continue;
        BinaryXmlElement element;
        element.name = LegacyName(*this, node.name_index);
        element.parent = open.empty() ? -1 : open.back();
        for (const auto& attr : node.attributes) {
            BinaryXmlAttribute a{LegacyName(*this, attr.namespace_index),
                                 LegacyName(*this, attr.name_index),
                                 attr.value.type,
                                 attr.value.data,
                                 {}};
            a.raw_string = LegacyString(*this, attr.raw_value_index != kNoIndex
                                                   ? attr.raw_value_index
                                                   : (attr.value.type == kTypeString ? attr.value.data : kNoIndex));
            if (a.namespace_uri == kAndroidNamespace) {
                if (a.name == "id")
                    element.id = a.data;
                else if (a.name == "layout_width")
                    element.layout_width = LayoutSize(a.value_type, a.data);
                else if (a.name == "layout_height")
                    element.layout_height = LayoutSize(a.value_type, a.data);
                else if (a.name == "gravity")
                    element.gravity = a.data;
                else if (a.name == "layout_gravity")
                    element.layout_gravity = a.data;
                else if (a.name == "paddingTop")
                    element.padding_top = DimensionPx(a.data);
                else if (a.name == "src")
                    element.src = a.data;
            }
            element.attributes.push_back(std::move(a));
        }
        open.push_back(static_cast<std::int32_t>(result.size()));
        result.push_back(std::move(element));
    }
    return result;
}

std::vector<BinaryXmlPullEvent> CompiledXmlDocument::PullEvents() const {
    std::vector<BinaryXmlPullEvent> result{{BinaryXmlPullEventType::start_document, {}, {}, 0}};
    std::int32_t depth{};
    for (const auto& node : nodes_) {
        if (node.type == kStartElementType)
            result.push_back({BinaryXmlPullEventType::start_tag, LegacyName(*this, node.name_index), {}, ++depth});
        else if (node.type == kEndElementType)
            result.push_back({BinaryXmlPullEventType::end_tag, LegacyName(*this, node.name_index), {}, depth--});
        else if (node.type == kCdataType)
            result.push_back({BinaryXmlPullEventType::text, {}, LegacyName(*this, node.text_index), depth});
    }
    result.push_back({BinaryXmlPullEventType::end_document, {}, {}, depth});
    return result;
}

std::shared_ptr<const CompiledXmlDocument> ParseCompiledXmlDocument(std::span<const std::byte> bytes) {
    return CompiledXmlDocumentBuilder::Parse(bytes, true);
}

std::vector<BinaryXmlElement> ParseBinaryXmlElements(std::span<const std::byte> bytes) {
    return CompiledXmlDocumentBuilder::Parse(bytes, false)->Elements();
}

std::vector<BinaryXmlPullEvent> ParseBinaryXmlPullEvents(std::span<const std::byte> bytes) {
    return CompiledXmlDocumentBuilder::Parse(bytes, false)->PullEvents();
}
}  // namespace ogplay::loader
