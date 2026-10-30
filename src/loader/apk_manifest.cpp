#include "ogplay/loader/apk_manifest.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_set>
#include <vector>

#include "ogplay/core/byte_order.h"
#include "ogplay/core/text.h"
#include "ogplay/loader/arsc.h"

namespace ogplay::loader {
namespace {

constexpr std::uint16_t kXmlType = 0x0003;
constexpr std::uint16_t kStringPoolType = 0x0001;
constexpr std::uint16_t kResourceMapType = 0x0180;
constexpr std::uint16_t kStartNamespaceType = 0x0100;
constexpr std::uint16_t kEndNamespaceType = 0x0101;
constexpr std::uint16_t kStartElementType = 0x0102;
constexpr std::uint16_t kEndElementType = 0x0103;
constexpr std::uint16_t kTextType = 0x0104;
constexpr std::uint32_t kNoIndex = 0xffffffffU;
constexpr std::uint32_t kUtf8Flag = 0x00000100U;
constexpr std::uint8_t kStringType = 0x03;
constexpr std::uint8_t kReferenceType = 0x01;
constexpr std::uint8_t kIntegerHexType = 0x11;
constexpr std::uint8_t kIntegerBooleanType = 0x12;
constexpr std::string_view kAndroidNamespace = "http://schemas.android.com/apk/res/android";
constexpr std::string_view kMainAction = "android.intent.action.MAIN";
constexpr std::string_view kLauncherCategory = "android.intent.category.LAUNCHER";

void RequireRange(const std::span<const std::byte> bytes, const std::size_t offset, const std::size_t size,
                  const std::string_view what) {
    if (!core::RangeFits(bytes, offset, size)) {
        throw std::runtime_error(std::string(what) + " is outside binary AndroidManifest");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 2, "binary XML field");
    return core::ReadLittleEndian<std::uint16_t>(bytes, offset);
}

std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 4, "binary XML field");
    return core::ReadLittleEndian<std::uint32_t>(bytes, offset);
}

struct Chunk final {
    std::uint16_t type{};
    std::uint16_t header_size{};
    std::uint32_t size{};
};

Chunk ReadChunk(const std::span<const std::byte> bytes, const std::size_t offset, const std::string_view what) {
    RequireRange(bytes, offset, 8, what);
    const Chunk chunk{Read16(bytes, offset), Read16(bytes, offset + 2), Read32(bytes, offset + 4)};
    if (chunk.header_size < 8 || chunk.size < chunk.header_size || ((chunk.header_size | chunk.size) & 3U) != 0U) {
        throw std::runtime_error(std::string(what) + " has an invalid chunk header");
    }
    RequireRange(bytes, offset, chunk.size, what);
    return chunk;
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
void AppendManifestUnit(std::string& output, const std::uint16_t unit) {
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
            AppendManifestUnit(result, static_cast<std::uint16_t>((code >> 10U) + 0xd800U));
            AppendManifestUnit(result, static_cast<std::uint16_t>((code & 0x3ffU) + 0xdc00U));
            units += 2;
        } else {
            AppendManifestUnit(result, static_cast<std::uint16_t>(code));
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
        AppendManifestUnit(result, unit);
    return result;
}

struct ManifestStrings final {
    std::span<const std::byte> bytes;
    std::vector<std::size_t> offsets;
    std::size_t end{};
    bool utf8{};
    mutable std::vector<std::optional<std::string>> cache;
    mutable std::vector<bool> decoded;

    std::optional<std::string_view> Name(const std::uint32_t index) const {
        // indexOfAttribute uses string8At for UTF8 pools: raw bytes and byte length,
        // without checking the declared UTF16 length or performing conversion.
        if (!utf8) {
            if (const auto* text = Get(index)) return *text;
            return std::nullopt;
        }
        if (index >= offsets.size()) return std::nullopt;
        try {
            auto cursor = offsets[index];
            static_cast<void>(ReadLength8(bytes, cursor, end));
            const auto length = ReadLength8(bytes, cursor, end);
            if (cursor > end || length >= end - cursor) return std::nullopt;
            return std::string_view{reinterpret_cast<const char*>(bytes.data() + cursor), length};
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
    }

    const std::string* Get(const std::uint32_t index) const {
        if (index >= offsets.size()) return nullptr;
        if (!decoded[index]) {
            decoded[index] = true;
            try {
                cache[index] =
                    utf8 ? ReadUtf8String(bytes, offsets[index], end) : ReadUtf16String(bytes, offsets[index], end);
            } catch (const std::runtime_error&) {
                // ResStringPool::stringAt returns NULL for an invalid accessed string.
            }
        }
        return cache[index] ? &*cache[index] : nullptr;
    }
};

ManifestStrings ReadStringPool(const std::span<const std::byte> bytes, const std::size_t offset, const Chunk& chunk) {
    if (chunk.type != kStringPoolType || chunk.header_size < 28) {
        throw std::runtime_error("binary AndroidManifest must begin with a string pool");
    }
    const auto count = static_cast<std::size_t>(Read32(bytes, offset + 8));
    const auto style_count = static_cast<std::size_t>(Read32(bytes, offset + 12));
    const auto flags = Read32(bytes, offset + 16);
    const auto strings_start = static_cast<std::size_t>(Read32(bytes, offset + 20));
    const auto styles_start = static_cast<std::size_t>(Read32(bytes, offset + 24));
    if (count == 0 || count > (chunk.size - chunk.header_size) / 4U ||
        style_count > (chunk.size - chunk.header_size) / 4U) {
        throw std::runtime_error("binary XML string pool count is invalid");
    }
    const auto table_bytes = (count + style_count) * 4U;
    if (table_bytes > chunk.size - chunk.header_size || strings_start < chunk.header_size + table_bytes ||
        strings_start >= chunk.size) {
        throw std::runtime_error("binary XML string pool offsets are invalid");
    }
    const auto strings_end = styles_start == 0 ? static_cast<std::size_t>(chunk.size) : styles_start;
    if (strings_end <= strings_start || strings_end > chunk.size) {
        throw std::runtime_error("binary XML string pool data range is invalid");
    }

    const bool utf8 = (flags & kUtf8Flag) != 0;
    const auto pool_end = offset + strings_end;
    if (utf8 && bytes[pool_end - 1] != std::byte{0})
        throw std::runtime_error("binary XML string pool is not terminated");
    ManifestStrings result{bytes, {}, offset + strings_end, (flags & kUtf8Flag) != 0, {}, {}};
    result.offsets.reserve(count);
    result.cache.resize(count);
    result.decoded.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        auto relative = static_cast<std::size_t>(Read32(bytes, offset + chunk.header_size + index * 4U));
        if (!utf8) relative &= ~std::size_t{1};
        // An invalid unused string is not a package parse error.
        result.offsets.push_back(relative < strings_end - strings_start ? offset + strings_start + relative
                                                                        : offset + strings_end);
    }
    return result;
}

const std::string& StringAt(const ManifestStrings& strings, const std::uint32_t index, const std::string_view field) {
    if (const auto* value = strings.Get(index)) return *value;
    throw std::runtime_error(std::string(field) + " string is unavailable");
}

bool IsPackageCharacter(const char value, const bool first) {
    const bool alpha = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    return alpha || (!first && ((value >= '0' && value <= '9') || value == '_'));
}

bool ValidPackage(const std::string_view package) {
    bool first = true;
    std::size_t components{1};
    for (const auto value : package) {
        if (value == '.') {
            first = true;
            ++components;
        } else {
            if (!IsPackageCharacter(value, first)) return false;
            first = false;
        }
    }
    return package == "android" || components >= 2;
}

std::uint32_t ManifestAttributeId(const std::string_view name) {
    // API19 core/res/res/values/public.xml; only fields projected by this parser.
    constexpr std::pair<std::string_view, std::uint32_t> ids[]{{"description", 0x01010020U},
                                                               {"enabled", 0x0101000eU},
                                                               {"exported", 0x01010010U},
                                                               {"icon", 0x01010002U},
                                                               {"isolatedProcess", 0x010103a9U},
                                                               {"label", 0x01010001U},
                                                               {"launchMode", 0x0101001dU},
                                                               {"largeHeap", 0x0101035aU},
                                                               {"maxSdkVersion", 0x01010271U},
                                                               {"minSdkVersion", 0x0101020cU},
                                                               {"name", 0x01010003U},
                                                               {"permission", 0x01010006U},
                                                               {"permissionFlags", 0x010103c7U},
                                                               {"permissionGroup", 0x0101000aU},
                                                               {"priority", 0x0101001cU},
                                                               {"process", 0x01010011U},
                                                               {"protectionLevel", 0x01010009U},
                                                               {"resource", 0x01010025U},
                                                               {"singleUser", 0x010103bfU},
                                                               {"stopWithTask", 0x0101036aU},
                                                               {"targetActivity", 0x01010202U},
                                                               {"targetSdkVersion", 0x01010270U},
                                                               {"taskAffinity", 0x01010012U},
                                                               {"theme", 0x01010000U},
                                                               {"value", 0x01010024U},
                                                               {"versionCode", 0x0101021bU},
                                                               {"versionName", 0x0101021cU}};
    for (const auto& [key, id] : ids)
        if (key == name) return id;
    return 0;
}

struct Attribute final {
    std::string_view name;
    std::optional<std::string_view> namespace_uri;
    std::uint32_t raw_value{kNoIndex};
    std::uint8_t data_type{};
    std::uint32_t data{};
    const AndroidManifestResourceResolver* resources{};
    std::optional<std::uint32_t> name_resource;
};

std::vector<Attribute> ReadAttributes(const std::span<const std::byte> bytes, const std::size_t offset,
                                      const Chunk& chunk, const ManifestStrings& strings,
                                      const AndroidManifestResourceResolver& resources,
                                      const std::optional<std::vector<std::uint32_t>>& resource_map) {
    if (chunk.header_size < 16 || chunk.size - chunk.header_size < 20) {
        throw std::runtime_error("binary XML start element header is invalid");
    }
    const auto extension = offset + chunk.header_size;
    const auto attribute_start = static_cast<std::size_t>(Read16(bytes, extension + 8));
    const auto attribute_size = static_cast<std::size_t>(Read16(bytes, extension + 10));
    const auto attribute_count = static_cast<std::size_t>(Read16(bytes, extension + 12));
    const auto extension_size = static_cast<std::size_t>(chunk.size - chunk.header_size);
    if (attribute_start < 20 || attribute_size < 20 || attribute_start > extension_size ||
        attribute_count > (extension_size - attribute_start) / attribute_size) {
        throw std::runtime_error("binary XML attribute table is invalid");
    }
    std::vector<Attribute> result;
    result.reserve(attribute_count);
    for (std::size_t index = 0; index < attribute_count; ++index) {
        const auto attribute = extension + attribute_start + index * attribute_size;
        const auto namespace_index = Read32(bytes, attribute);
        const auto raw_value = Read32(bytes, attribute + 8);
        const auto data_type = std::to_integer<std::uint8_t>(bytes[attribute + 15]);
        const auto data = Read32(bytes, attribute + 16);
        const auto namespace_text = strings.Name(namespace_index);
        if (namespace_index != kNoIndex && !namespace_text && !resource_map) continue;
        const auto namespace_uri = namespace_text ? std::optional<std::string_view>{*namespace_text} : std::nullopt;
        const auto name_index = Read32(bytes, attribute + 4);
        const auto attribute_name = strings.Name(name_index);
        if (!attribute_name && !resource_map) continue;
        result.push_back(
            {attribute_name ? std::string_view{*attribute_name} : std::string_view{}, namespace_uri, raw_value,
             data_type, data, &resources,
             resource_map
                 ? std::optional<std::uint32_t>{name_index < resource_map->size() ? (*resource_map)[name_index] : 0U}
                 : std::nullopt});
    }
    return result;
}

const Attribute* FindAttribute(const std::vector<Attribute>& attributes, const std::string_view name,
                               const std::optional<std::string_view> namespace_uri) {
    const Attribute* result{};
    const auto expected_id = namespace_uri == kAndroidNamespace ? ManifestAttributeId(name) : 0U;
    for (const auto& attribute : attributes) {
        if (expected_id && attribute.name_resource) {
            // TypedArray advances to the first resource id >= the requested id.
            if (*attribute.name_resource > expected_id) break;
            if (*attribute.name_resource != expected_id) continue;
        } else if (attribute.name != name || attribute.namespace_uri != namespace_uri)
            continue;
        result = &attribute;
        break;
    }
    if (result && (result->data_type == 0U || (result->data_type == kReferenceType && result->data == 0U)))
        return nullptr;
    return result;
}

AndroidManifestResourceValue ResolveAttribute(const Attribute& attribute, const ManifestStrings& strings) {
    AndroidManifestResourceValue value{attribute.data_type, attribute.data, {}};
    if (value.type == kStringType) {
        if (const auto* text = strings.Get(value.data)) value.string = *text;
    }
    std::array<std::uint32_t, 32> seen{};
    std::size_t count{};
    while (value.type == kReferenceType && value.data != 0U) {
        if (count == seen.size() || std::find(seen.begin(), seen.begin() + count, value.data) != seen.begin() + count)
            throw std::runtime_error("binary AndroidManifest resource cycle or depth limit");
        seen[count++] = value.data;
        if (!attribute.resources || !*attribute.resources)
            throw std::runtime_error("binary AndroidManifest resource context is missing: " +
                                     std::to_string(value.data));
        const auto next = (*attribute.resources)(value.data);
        if (!next)
            throw std::runtime_error("binary AndroidManifest resource is missing: " + std::to_string(value.data));
        value = *next;
    }
    if (value.type == kReferenceType) value.type = 0;
    return value;
}

std::string CoerceManifestString(const AndroidManifestResourceValue& value) {
    if (value.type == kStringType && value.string) return *value.string;
    if (value.type == 0) return {};
    if (value.type == 0x02U) return "?" + std::to_string(value.data);
    if (value.type == kIntegerBooleanType) return value.data ? "true" : "false";
    if (value.type == 0x04U) {
        const auto number = std::bit_cast<float>(value.data);
        if (std::isnan(number)) return "NaN";
        if (std::isinf(number)) return number < 0 ? "-Infinity" : "Infinity";
        std::array<char, 64> buffer{};
        const auto magnitude = std::abs(number);
        const auto format = magnitude != 0 && (magnitude < 0.001F || magnitude >= 10000000.0F)
                                ? std::chars_format::scientific
                                : std::chars_format::fixed;
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number, format);
        if (result.ec != std::errc{}) throw std::runtime_error("Manifest float conversion failed");
        auto text = std::string(buffer.data(), result.ptr);
        const auto exponent = text.find('e');
        auto mantissa = text.substr(0, exponent);
        if (mantissa.find('.') == std::string::npos) mantissa += ".0";
        if (exponent != std::string::npos) mantissa += "E" + std::to_string(std::stoi(text.substr(exponent + 1)));
        return mantissa;
    }
    if (value.type == 0x05U || value.type == 0x06U) {
        constexpr std::array<float, 4> radix{1.0F / 256, 1.0F / 32768, 1.0F / 8388608, 1.0F / 2147483648.0F};
        constexpr std::array<const char*, 6> units{"px", "dip", "sp", "pt", "in", "mm"};
        const auto unit = value.data & 0xfU;
        if ((value.type == 0x05U && unit >= units.size()) || (value.type == 0x06U && unit > 1U))
            throw std::runtime_error("Manifest complex value has invalid unit");
        auto number =
            static_cast<float>(std::bit_cast<std::int32_t>(value.data & 0xffffff00U)) * radix[(value.data >> 4U) & 3U];
        if (value.type == 0x06U) number *= 100.0F;
        return CoerceManifestString({0x04U, std::bit_cast<std::uint32_t>(number), {}}) + (value.type == 0x05U
                                                                                              ? units[unit]
                                                                                          : unit == 0U ? "%"
                                                                                                       : "%p");
    }
    if (value.type == kIntegerHexType || (value.type >= 0x1cU && value.type <= 0x1fU)) {
        std::array<char, 8> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.data, 16);
        return std::string(value.type == kIntegerHexType ? "0x" : "#") + std::string(buffer.data(), result.ptr);
    }
    if (value.type >= 0x10U && value.type <= 0x1fU) return std::to_string(std::bit_cast<std::int32_t>(value.data));
    throw std::runtime_error("binary AndroidManifest value cannot be coerced to string: " + std::to_string(value.type));
}

std::optional<std::string> ReadOptionalStringAttribute(const Attribute& attribute, const ManifestStrings& strings) {
    const auto value = ResolveAttribute(attribute, strings);
    if (value.type == 0U || (value.type == kStringType && !value.string)) return std::nullopt;
    return CoerceManifestString(value);
}

std::string ReadStringAttribute(const Attribute& attribute, const ManifestStrings& strings,
                                const std::string_view name) {
    static_cast<void>(name);
    return ReadOptionalStringAttribute(attribute, strings).value_or("");
}

std::string ReadNonResourceStringAttribute(const Attribute& attribute, const ManifestStrings& strings,
                                           const std::string_view name) {
    if (attribute.data_type != kStringType)
        throw std::runtime_error("binary AndroidManifest attribute " + std::string(name) + " is not a string");
    return StringAt(strings, attribute.data, "Manifest non-resource string");
}

std::uint32_t ReadIntegerAttribute(const Attribute& attribute, const std::string_view name,
                                   const ManifestStrings& strings, const bool coerce = false) {
    const auto value = ResolveAttribute(attribute, strings);
    if (value.type == 0) return 0;
    if (value.type >= 0x10U && value.type <= 0x1fU) return value.data;
    if (coerce && value.type == kStringType && !value.string) return 0;
    if (coerce) {
        const auto text = CoerceManifestString(value);
        // XmlUtils.convertValueToInt strips an initial minus, then parses
        // a signed int in decimal, octal, 0x or # notation without whitespace.
        auto numeric = std::string_view(text);
        const bool negative = numeric.starts_with('-');
        if (negative) numeric.remove_prefix(1);
        int base = 10;
        if (numeric.starts_with('0')) {
            if (numeric.size() == 1) return 0U;
            if (numeric[1] == 'x' || numeric[1] == 'X') {
                base = 16;
                numeric.remove_prefix(2);
            } else {
                base = 8;
                numeric.remove_prefix(1);
            }
        } else if (numeric.starts_with('#')) {
            base = 16;
            numeric.remove_prefix(1);
        }
        const bool positive = numeric.starts_with('+');
        if (positive) numeric.remove_prefix(1);
        std::int32_t number{};
        const auto parsed = std::from_chars(numeric.data(), numeric.data() + numeric.size(), number, base);
        if (!numeric.empty() && !(positive && numeric.starts_with('-')) && parsed.ec == std::errc{} &&
            parsed.ptr == numeric.data() + numeric.size()) {
            const auto bits = static_cast<std::uint32_t>(number);
            return negative ? 0U - bits : bits;
        }
    }
    throw std::runtime_error("binary AndroidManifest attribute " + std::string(name) + " is not an integer");
}

std::uint32_t ReadReferenceAttribute(const Attribute& attribute, const std::string_view name) {
    static_cast<void>(name);
    return attribute.data_type == kReferenceType ? attribute.data : 0U;
}

bool ReadBooleanAttribute(const Attribute& attribute, const std::string_view name, const ManifestStrings& strings,
                          const bool default_value = false) {
    static_cast<void>(name);
    const auto value = ResolveAttribute(attribute, strings);
    if (value.type == 0 || (value.type == kStringType && !value.string)) return default_value;
    if (value.type >= 0x10U && value.type <= 0x1fU) return value.data != 0;
    const auto text = CoerceManifestString(value);
    return text == "1" || text == "true" || text == "TRUE";
}

std::optional<AndroidManifestMetaDataValue> ReadMetaDataValue(const Attribute& attribute,
                                                              const ManifestStrings& strings,
                                                              const std::string_view name,
                                                              std::vector<std::string>& diagnostics) {
    // The byte-only API can retain a value reference for a later resource owner.
    if (attribute.data_type == kReferenceType && attribute.data != 0U &&
        (!attribute.resources || !*attribute.resources))
        return AndroidManifestMetaDataValueReference{attribute.data};
    const auto value = ResolveAttribute(attribute, strings);
    if (value.type == 0U)
        throw std::runtime_error("binary AndroidManifest meta-data requires value or resource: " + std::string(name));
    if (value.type == kStringType) {
        if (value.string) return *value.string;
        return AndroidManifestMetaDataNullString{};
    }
    if (value.type == kIntegerBooleanType) return value.data != 0U;
    if (value.type >= 0x10U && value.type <= 0x1fU) return std::bit_cast<std::int32_t>(value.data);
    if (value.type == 0x04U) return std::bit_cast<float>(value.data);
    diagnostics.push_back("ignored unsupported meta-data type " + std::to_string(value.type) + ": " +
                          std::string(name));
    return std::nullopt;
}

}  // namespace

AndroidManifestStartupError::AndroidManifestStartupError(const AndroidManifestStartupErrorReason reason,
                                                         std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

AndroidManifestStartupErrorReason AndroidManifestStartupError::Reason() const noexcept {
    return reason_;
}

std::string NormalizeAndroidManifestClassName(const std::string_view package, const std::string_view class_name) {
    if (class_name.empty()) {
        throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::invalid_class_name,
                                          "binary AndroidManifest contains an empty class name");
    }
    if (class_name.front() == '.') return std::string(package) + std::string(class_name);
    if (class_name.find('.') == std::string_view::npos) {
        return std::string(package) + "." + std::string(class_name);
    }
    if (class_name.front() >= 'a' && class_name.front() <= 'z') {
        return std::string(class_name);
    }
    throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::invalid_class_name,
                                      "binary AndroidManifest contains invalid class name " + std::string(class_name));
}

namespace {
std::string ManifestProcessName(const std::string_view package, const std::string_view declared,
                                const std::string_view fallback) {
    if (declared.empty()) return std::string(fallback);
    const bool private_process = declared.front() == ':';
    const auto name = private_process ? declared.substr(1) : declared;
    bool segment_start = true;
    bool has_separator = false;
    for (const char character : name) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
            segment_start = false;
        } else if (!segment_start && ((character >= '0' && character <= '9') || character == '_')) {
            continue;
        } else if (character == '.') {
            has_separator = true;
            segment_start = true;
        } else {
            throw std::runtime_error("binary AndroidManifest has an invalid process name");
        }
    }
    if (name.empty() || (!private_process && !has_separator && name != "system"))
        throw std::runtime_error("binary AndroidManifest has an invalid process name");
    if (private_process) return std::string(package) + std::string(declared);
    return std::string(declared);
}
}  // namespace

const AndroidManifestActivityComponent*
FindAndroidManifestActivityComponent(const std::span<const AndroidManifestActivityComponent> components,
                                     const std::string_view name) {
    for (auto index = components.size(); index != 0; --index) {
        if (components[index - 1].name == name) return &components[index - 1];
    }
    return nullptr;
}

AndroidManifestLauncherComponent ResolveLauncherComponent(const AndroidManifestFacts& facts) {
    const auto has_value = [](const std::vector<std::string>& values, const std::string_view expected) {
        return std::find(values.begin(), values.end(), expected) != values.end();
    };
    for (const auto& component : facts.activity_components) {
        if (!component.enabled) continue;
        const auto launcher = std::any_of(component.intent_filters.begin(), component.intent_filters.end(),
                                          [&](const AndroidManifestIntentFilter& filter) {
                                              return has_value(filter.actions, kMainAction) &&
                                                     has_value(filter.categories, kLauncherCategory);
                                          });
        if (!launcher) continue;

        const auto* registered = FindAndroidManifestActivityComponent(facts.activity_components, component.name);
        if (!registered->enabled) continue;

        if (registered->kind == AndroidManifestComponentKind::activity) {
            return {registered->name, registered->name, false};
        }
        const auto target = std::find_if(facts.activity_components.begin(), facts.activity_components.end(),
                                         [&](const AndroidManifestActivityComponent& candidate) {
                                             return candidate.kind == AndroidManifestComponentKind::activity &&
                                                    registered->target_activity.has_value() &&
                                                    candidate.name == *registered->target_activity;
                                         });
        if (target != facts.activity_components.end()) {
            return {registered->name, target->name, true};
        }
    }
    throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::no_launcher,
                                      "binary AndroidManifest has no enabled MAIN/LAUNCHER activity");
}

AndroidManifestFacts ParseAndroidBinaryManifest(const std::span<const std::byte> bytes,
                                                const AndroidManifestResourceResolver& resources) {
    const auto xml = ReadChunk(bytes, 0, "binary AndroidManifest");
    if (xml.type != kXmlType || xml.header_size < 8) {
        throw std::runtime_error("AndroidManifest.xml is not one complete binary XML document");
    }
    std::size_t cursor = xml.header_size;
    const auto document = bytes.first(xml.size);
    std::optional<ManifestStrings> pool;
    std::optional<std::vector<std::uint32_t>> resource_map;
    const auto read_resource_map = [&](const std::size_t at, const Chunk& chunk) {
        resource_map.emplace();
        for (std::size_t i = chunk.header_size; i + 4 <= chunk.size; i += 4)
            resource_map->push_back(Read32(document, at + i));
    };
    while (cursor < document.size()) {
        const auto chunk = ReadChunk(document, cursor, "binary XML preamble");
        if (chunk.type == kStringPoolType)
            pool = ReadStringPool(document, cursor, chunk);
        else if (chunk.type == kResourceMapType)
            read_resource_map(cursor, chunk);
        else if (chunk.type >= kStartNamespaceType && chunk.type <= kTextType)
            break;
        cursor += chunk.size;
    }
    if (!pool) throw std::runtime_error("binary AndroidManifest has no string pool");
    const auto& strings = *pool;

    AndroidManifestFacts facts;
    std::vector<std::string> elements;
    bool saw_manifest{};
    bool saw_application{};
    // Fixed platform groups from API19 core/res/AndroidManifest.xml; no installed-package registry.
    std::unordered_set<std::string> permission_groups{"android.permission-group.COST_MONEY",
                                                      "android.permission-group.MESSAGES",
                                                      "android.permission-group.SOCIAL_INFO",
                                                      "android.permission-group.PERSONAL_INFO",
                                                      "android.permission-group.CALENDAR",
                                                      "android.permission-group.USER_DICTIONARY",
                                                      "android.permission-group.WRITE_USER_DICTIONARY",
                                                      "android.permission-group.BOOKMARKS",
                                                      "android.permission-group.DEVICE_ALARMS",
                                                      "android.permission-group.VOICEMAIL",
                                                      "android.permission-group.ACCESSIBILITY_FEATURES",
                                                      "android.permission-group.LOCATION",
                                                      "android.permission-group.NETWORK",
                                                      "android.permission-group.BLUETOOTH_NETWORK",
                                                      "android.permission-group.ACCOUNTS",
                                                      "android.permission-group.AFFECTS_BATTERY",
                                                      "android.permission-group.AUDIO_SETTINGS",
                                                      "android.permission-group.HARDWARE_CONTROLS",
                                                      "android.permission-group.MICROPHONE",
                                                      "android.permission-group.CAMERA",
                                                      "android.permission-group.PHONE_CALLS",
                                                      "android.permission-group.STORAGE",
                                                      "android.permission-group.SCREENLOCK",
                                                      "android.permission-group.APP_INFO",
                                                      "android.permission-group.DISPLAY",
                                                      "android.permission-group.WALLPAPER",
                                                      "android.permission-group.SYSTEM_CLOCK",
                                                      "android.permission-group.STATUS_BAR",
                                                      "android.permission-group.SYNC_SETTINGS",
                                                      "android.permission-group.SYSTEM_TOOLS",
                                                      "android.permission-group.DEVELOPMENT_TOOLS"};
    std::optional<std::size_t> skip_depth;
    std::optional<std::string> application_task_affinity;
    std::optional<std::size_t> current_component;
    std::optional<std::size_t> current_service;
    std::optional<std::size_t> current_receiver;
    std::optional<std::size_t> current_permission;
    std::optional<AndroidManifestIntentFilter> current_intent_filter;
    while (cursor < document.size()) {
        const auto chunk = ReadChunk(document, cursor, "binary XML node");
        if (chunk.type == kStartElementType) {
            if (skip_depth) {
                if (chunk.header_size < 16 || chunk.size - chunk.header_size < 20)
                    throw std::runtime_error("binary XML skipped start node is truncated");
                const auto extension = cursor + chunk.header_size;
                const auto start = Read16(document, extension + 8);
                const auto stride = Read16(document, extension + 10);
                const auto count = Read16(document, extension + 12);
                if (static_cast<std::size_t>(start) + static_cast<std::size_t>(stride) * count >
                    chunk.size - chunk.header_size)
                    throw std::runtime_error("binary XML skipped attribute table is truncated");
                elements.emplace_back();
                cursor += chunk.size;
                continue;
            }
            const auto attributes = ReadAttributes(document, cursor, chunk, strings, resources, resource_map);
            const auto namespace_index = Read32(bytes, cursor + chunk.header_size);
            if (namespace_index != kNoIndex) {
                static_cast<void>(StringAt(strings, namespace_index, "binary XML element namespace"));
            }
            const auto name =
                StringAt(strings, Read32(bytes, cursor + chunk.header_size + 4), "binary XML element name");
            if (elements.empty()) {
                if (saw_manifest || name != "manifest") {
                    throw std::runtime_error("binary AndroidManifest root element is invalid");
                }
                saw_manifest = true;
                const auto* package = FindAttribute(attributes, "package", std::nullopt);
                const auto* version_code = FindAttribute(attributes, "versionCode", kAndroidNamespace);
                if (package == nullptr) {
                    throw std::runtime_error("binary AndroidManifest is missing package");
                }
                facts.package = ReadNonResourceStringAttribute(*package, strings, "package");
                facts.application_process_name = facts.package;
                if (version_code) facts.version_code = ReadIntegerAttribute(*version_code, "versionCode", strings);
                if (const auto* version_name = FindAttribute(attributes, "versionName", kAndroidNamespace)) {
                    facts.version_name = ReadOptionalStringAttribute(*version_name, strings);
                }
            } else if (name == "application" && elements.size() == 1) {
                if (saw_application) {
                    facts.diagnostics.push_back("ignored duplicate application");
                    skip_depth = elements.size();
                    elements.push_back(name);
                    cursor += chunk.size;
                    continue;
                }
                saw_application = true;
                if (const auto* large_heap = FindAttribute(attributes, "largeHeap", kAndroidNamespace))
                    facts.application_large_heap = ReadBooleanAttribute(*large_heap, "application largeHeap", strings);
                if (const auto* affinity = FindAttribute(attributes, "taskAffinity", kAndroidNamespace))
                    application_task_affinity = ReadOptionalStringAttribute(*affinity, strings);
                if (const auto* enabled = FindAttribute(attributes, "enabled", kAndroidNamespace)) {
                    facts.application_enabled = ReadBooleanAttribute(*enabled, "application enabled", strings, true);
                }
                if (const auto* process = FindAttribute(attributes, "process", kAndroidNamespace)) {
                    facts.application_process_name = ManifestProcessName(
                        facts.package, ReadStringAttribute(*process, strings, "application process"), facts.package);
                }
                if (const auto* permission = FindAttribute(attributes, "permission", kAndroidNamespace)) {
                    const auto value = ReadStringAttribute(*permission, strings, "application permission");
                    if (!value.empty()) facts.application_permission = value;
                }
                if (const auto* theme = FindAttribute(attributes, "theme", kAndroidNamespace))
                    facts.application_theme = ReadReferenceAttribute(*theme, "application theme");
                if (const auto* icon = FindAttribute(attributes, "icon", kAndroidNamespace)) {
                    facts.application_icon = ReadReferenceAttribute(*icon, "application icon");
                }
                if (const auto* label = FindAttribute(attributes, "label", kAndroidNamespace)) {
                    if (label->data_type == kReferenceType) {
                        facts.application_label = ReadReferenceAttribute(*label, "application label");
                    } else {
                        facts.application_label = ReadStringAttribute(*label, strings, "application label");
                    }
                }
                if (const auto* application_name = FindAttribute(attributes, "name", kAndroidNamespace)) {
                    if (const auto value = ReadOptionalStringAttribute(*application_name, strings))
                        facts.application_class = NormalizeAndroidManifestClassName(facts.package, *value);
                }
            } else if (name == "meta-data" &&
                       ((elements.size() == 2 && elements[1] == "application") ||
                        (elements.size() == 3 && current_component &&
                         (elements[2] == "activity" || elements[2] == "activity-alias")) ||
                        (elements.size() == 3 && elements[2] == "receiver" && current_receiver.has_value()) ||
                        (elements.size() == 3 && elements[2] == "service" && current_service.has_value()) ||
                        (elements.size() == 2 && elements[1] == "permission" && current_permission.has_value()))) {
                auto& meta_data = current_component    ? facts.activity_components[*current_component].meta_data
                                  : current_receiver   ? facts.receiver_components[*current_receiver].meta_data
                                  : current_service    ? facts.service_components[*current_service].meta_data
                                  : current_permission ? facts.defined_permissions[*current_permission].meta_data
                                                       : facts.application_meta_data;
                const auto owner = current_component    ? "activity"
                                   : current_receiver   ? "receiver"
                                   : current_service    ? "service"
                                   : current_permission ? "permission"
                                                        : "application";
                const auto* metadata_name = FindAttribute(attributes, "name", kAndroidNamespace);
                const auto* metadata_value = FindAttribute(attributes, "value", kAndroidNamespace);
                const auto* metadata_resource = FindAttribute(attributes, "resource", kAndroidNamespace);
                if (metadata_name == nullptr)
                    throw std::runtime_error("binary AndroidManifest meta-data requires name");
                const auto decoded = ReadOptionalStringAttribute(*metadata_name, strings);
                if (!decoded) throw std::runtime_error("binary AndroidManifest meta-data requires name");
                const auto& decoded_name = *decoded;
                std::optional<AndroidManifestMetaDataValue> value;
                const auto resource_id =
                    metadata_resource ? ReadReferenceAttribute(*metadata_resource, "meta-data resource") : 0U;
                if (resource_id != 0U)
                    value = AndroidManifestMetaDataResourceReference{resource_id};
                else if (metadata_value)
                    value = ReadMetaDataValue(*metadata_value, strings, decoded_name, facts.diagnostics);
                else
                    throw std::runtime_error("binary AndroidManifest " + std::string(owner) +
                                             " meta-data requires value or resource");
                if (value) {
                    const auto existing = std::find_if(meta_data.begin(), meta_data.end(),
                                                       [&](const auto& item) { return item.name == decoded_name; });
                    if (existing == meta_data.end())
                        meta_data.push_back({decoded_name, std::move(*value)});
                    else
                        existing->value = std::move(*value);
                }
            } else if ((name == "activity" || name == "activity-alias") && elements.size() == 2 &&
                       elements[1] == "application") {
                const auto* component_name = FindAttribute(attributes, "name", kAndroidNamespace);
                if (component_name == nullptr) {
                    throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::missing_component_name,
                                                      "binary AndroidManifest " + name +
                                                          " does not specify android:name");
                }
                AndroidManifestActivityComponent component;
                component.kind = name == "activity" ? AndroidManifestComponentKind::activity
                                                    : AndroidManifestComponentKind::activity_alias;
                component.name = NormalizeAndroidManifestClassName(
                    facts.package, ReadStringAttribute(*component_name, strings, name + " name"));
                if (const auto* enabled = FindAttribute(attributes, "enabled", kAndroidNamespace)) {
                    component.enabled = ReadBooleanAttribute(*enabled, name + " enabled", strings, true);
                }
                if (const auto* exported = FindAttribute(attributes, "exported", kAndroidNamespace)) {
                    if (ResolveAttribute(*exported, strings).type != 0U)
                        component.exported = ReadBooleanAttribute(*exported, name + " exported", strings);
                }
                if (component.kind == AndroidManifestComponentKind::activity) {
                    component.task_affinity = application_task_affinity.value_or(facts.package);
                    if (const auto* affinity = FindAttribute(attributes, "taskAffinity", kAndroidNamespace))
                        if (const auto value = ReadOptionalStringAttribute(*affinity, strings))
                            component.task_affinity = *value;
                    if (const auto* mode = FindAttribute(attributes, "launchMode", kAndroidNamespace)) {
                        component.launch_mode = ReadIntegerAttribute(*mode, "activity launchMode", strings, true);
                    }
                    if (const auto* theme = FindAttribute(attributes, "theme", kAndroidNamespace))
                        component.theme = ReadReferenceAttribute(*theme, "activity theme");
                }
                if (component.kind == AndroidManifestComponentKind::activity_alias) {
                    const auto* target = FindAttribute(attributes, "targetActivity", kAndroidNamespace);
                    if (target == nullptr) {
                        throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::missing_alias_target,
                                                          "binary AndroidManifest activity-alias does not specify "
                                                          "android:targetActivity");
                    }
                    component.target_activity = NormalizeAndroidManifestClassName(
                        facts.package, ReadStringAttribute(*target, strings, "activity-alias targetActivity"));
                    const auto target_activity =
                        std::find_if(facts.activity_components.begin(), facts.activity_components.end(),
                                     [&](const AndroidManifestActivityComponent& candidate) {
                                         return candidate.kind == AndroidManifestComponentKind::activity &&
                                                candidate.name == *component.target_activity;
                                     });
                    if (target_activity == facts.activity_components.end()) {
                        throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::alias_target_not_found,
                                                          "binary AndroidManifest activity-alias target " +
                                                              *component.target_activity +
                                                              " was not declared before the alias");
                    }
                    component.theme = target_activity->theme;
                    component.launch_mode = target_activity->launch_mode;
                    component.task_affinity = target_activity->task_affinity;
                }
                facts.activity_components.push_back(std::move(component));
                current_component = facts.activity_components.size() - 1U;
                current_intent_filter.reset();
            } else if (name == "service" && elements.size() == 2 && elements[1] == "application") {
                const auto* service_name = FindAttribute(attributes, "name", kAndroidNamespace);
                if (service_name == nullptr) {
                    throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::missing_component_name,
                                                      "binary AndroidManifest service does not specify android:name");
                }
                AndroidManifestServiceComponent service;
                service.name = NormalizeAndroidManifestClassName(
                    facts.package, ReadStringAttribute(*service_name, strings, "service name"));
                if (const auto* enabled = FindAttribute(attributes, "enabled", kAndroidNamespace)) {
                    service.enabled = ReadBooleanAttribute(*enabled, "service enabled", strings, true);
                }
                if (const auto* exported = FindAttribute(attributes, "exported", kAndroidNamespace)) {
                    if (ResolveAttribute(*exported, strings).type != 0U)
                        service.exported = ReadBooleanAttribute(*exported, "service exported", strings);
                }
                service.process_name = facts.application_process_name;
                if (const auto* process = FindAttribute(attributes, "process", kAndroidNamespace)) {
                    service.process_name =
                        ManifestProcessName(facts.package, ReadStringAttribute(*process, strings, "service process"),
                                            facts.application_process_name);
                }
                service.permission = facts.application_permission;
                if (const auto* permission = FindAttribute(attributes, "permission", kAndroidNamespace)) {
                    if (const auto value = ReadOptionalStringAttribute(*permission, strings))
                        service.permission = value->empty() ? std::nullopt : value;
                }
                for (const auto& [attribute, bit] :
                     {std::pair{"stopWithTask", 0x00000001U}, std::pair{"isolatedProcess", 0x00000002U},
                      std::pair{"singleUser", 0x40000000U}}) {
                    if (const auto* value = FindAttribute(attributes, attribute, kAndroidNamespace);
                        value != nullptr && ReadBooleanAttribute(*value, attribute, strings)) {
                        service.flags |= bit;
                    }
                }
                if ((service.flags & 0x40000000U) != 0U) service.exported = false;
                facts.service_components.push_back(std::move(service));
                current_service = facts.service_components.size() - 1U;
                current_intent_filter.reset();
            } else if (name == "receiver" && elements.size() == 2 && elements[1] == "application") {
                const auto* receiver_name = FindAttribute(attributes, "name", kAndroidNamespace);
                if (receiver_name == nullptr) {
                    throw AndroidManifestStartupError(AndroidManifestStartupErrorReason::missing_component_name,
                                                      "binary AndroidManifest receiver does not specify android:name");
                }
                AndroidManifestReceiverComponent receiver;
                receiver.name = NormalizeAndroidManifestClassName(
                    facts.package, ReadStringAttribute(*receiver_name, strings, "receiver name"));
                if (const auto* enabled = FindAttribute(attributes, "enabled", kAndroidNamespace)) {
                    receiver.enabled = ReadBooleanAttribute(*enabled, "receiver enabled", strings, true);
                }
                if (const auto* exported = FindAttribute(attributes, "exported", kAndroidNamespace)) {
                    if (ResolveAttribute(*exported, strings).type != 0U)
                        receiver.exported = ReadBooleanAttribute(*exported, "receiver exported", strings);
                }
                receiver.process_name = facts.application_process_name;
                if (const auto* process = FindAttribute(attributes, "process", kAndroidNamespace)) {
                    receiver.process_name =
                        ManifestProcessName(facts.package, ReadStringAttribute(*process, strings, "receiver process"),
                                            facts.application_process_name);
                }
                receiver.permission = facts.application_permission;
                if (const auto* permission = FindAttribute(attributes, "permission", kAndroidNamespace)) {
                    if (const auto value = ReadOptionalStringAttribute(*permission, strings))
                        receiver.permission = value->empty() ? std::nullopt : value;
                }
                facts.receiver_components.push_back(std::move(receiver));
                current_receiver = facts.receiver_components.size() - 1U;
                current_intent_filter.reset();
            } else if (name == "intent-filter" && (current_component || current_service || current_receiver) &&
                       elements.size() == 3 &&
                       (elements.back() == "activity" || elements.back() == "activity-alias" ||
                        elements.back() == "service" || elements.back() == "receiver")) {
                current_intent_filter.emplace();
                if (const auto* priority = FindAttribute(attributes, "priority", kAndroidNamespace)) {
                    current_intent_filter->priority = std::bit_cast<std::int32_t>(
                        ReadIntegerAttribute(*priority, "intent-filter priority", strings, true));
                }
                if (const auto* label = FindAttribute(attributes, "label", kAndroidNamespace)) {
                    current_intent_filter->label =
                        label->data_type == kReferenceType
                            ? AndroidManifestLabel{ReadReferenceAttribute(*label, "intent-filter label")}
                            : AndroidManifestLabel{ReadStringAttribute(*label, strings, "intent-filter label")};
                }
                if (const auto* icon = FindAttribute(attributes, "icon", kAndroidNamespace)) {
                    current_intent_filter->icon = ReadReferenceAttribute(*icon, "intent-filter icon");
                }
            } else if (name == "action" && (current_component || current_service || current_receiver) &&
                       current_intent_filter.has_value() && elements.size() == 4 &&
                       elements.back() == "intent-filter") {
                if (const auto* action_name = FindAttribute(attributes, "name", kAndroidNamespace)) {
                    current_intent_filter->actions.push_back(ReadStringAttribute(*action_name, strings, "action name"));
                }
            } else if (name == "category" && (current_component || current_service || current_receiver) &&
                       current_intent_filter.has_value() && elements.size() == 4 &&
                       elements.back() == "intent-filter") {
                if (const auto* category_name = FindAttribute(attributes, "name", kAndroidNamespace)) {
                    current_intent_filter->categories.push_back(
                        ReadStringAttribute(*category_name, strings, "category name"));
                }
            } else if (name == "data" && current_intent_filter && elements.size() == 4 &&
                       elements.back() == "intent-filter") {
                current_intent_filter->has_data = true;
            } else if (elements.size() == 1 && name == "uses-sdk") {
                const auto read_sdk = [&](const Attribute& attribute) {
                    const auto value = ResolveAttribute(attribute, strings);
                    if (value.type == kStringType)
                        throw std::runtime_error("binary AndroidManifest requires development SDK: " +
                                                 value.string.value_or(""));
                    return value.data;
                };
                facts.min_sdk = 0U;
                if (const auto* min_sdk = FindAttribute(attributes, "minSdkVersion", kAndroidNamespace))
                    facts.min_sdk = read_sdk(*min_sdk);
                facts.target_sdk = *facts.min_sdk;
                if (const auto* target_sdk = FindAttribute(attributes, "targetSdkVersion", kAndroidNamespace))
                    facts.target_sdk = read_sdk(*target_sdk);
            } else if (elements.size() == 1 && name == "permission-group") {
                const auto* group_name = FindAttribute(attributes, "name", kAndroidNamespace);
                if (!group_name) throw std::runtime_error("binary AndroidManifest permission-group has no name");
                permission_groups.insert(NormalizeAndroidManifestClassName(
                    facts.package, ReadStringAttribute(*group_name, strings, "permission-group name")));
            } else if (elements.size() == 1 && name == "permission") {
                const auto* permission_name = FindAttribute(attributes, "name", kAndroidNamespace);
                if (permission_name == nullptr) {
                    throw std::runtime_error("binary AndroidManifest permission has no name");
                }
                AndroidManifestPermissionDefinition definition;
                definition.name = NormalizeAndroidManifestClassName(
                    facts.package, ReadStringAttribute(*permission_name, strings, "permission name"));
                definition.package_name = facts.package;
                if (const auto* level = FindAttribute(attributes, "protectionLevel", kAndroidNamespace)) {
                    definition.protection_level =
                        ReadIntegerAttribute(*level, "permission protectionLevel", strings, true);
                }
                if (definition.protection_level == 3U) definition.protection_level = 0x12U;
                if (definition.protection_level == 0xffffffffU ||
                    ((definition.protection_level & 0xf0U) != 0U && (definition.protection_level & 0xfU) != 2U)) {
                    throw std::runtime_error("binary AndroidManifest permission protectionLevel is invalid");
                }
                if (const auto* group = FindAttribute(attributes, "permissionGroup", kAndroidNamespace)) {
                    if (group->data_type == kStringType)
                        if (const auto* value = strings.Get(group->data)) definition.group = *value;
                }
                if (const auto* flags = FindAttribute(attributes, "permissionFlags", kAndroidNamespace)) {
                    definition.flags = ReadIntegerAttribute(*flags, "permission flags", strings, true);
                }
                if (const auto* description = FindAttribute(attributes, "description", kAndroidNamespace)) {
                    if (description->data_type == kReferenceType)
                        definition.description_res = ReadReferenceAttribute(*description, "permission description");
                }
                facts.defined_permissions.push_back(std::move(definition));
                current_permission = facts.defined_permissions.size() - 1U;
            } else if (elements.size() == 1 && name == "supports-gl-texture") {
                // ADR-0128 consumes this raw APK fact independently of PackageParser.
                const auto* texture_name = FindAttribute(attributes, "name", kAndroidNamespace);
                const auto* texture =
                    texture_name && texture_name->data_type == kStringType ? strings.Get(texture_name->data) : nullptr;
                if (!texture || texture->empty()) {
                    facts.diagnostics.push_back("skipped supports-gl-texture with unavailable or empty android:name");
                } else if (std::find(facts.supported_gl_textures.begin(), facts.supported_gl_textures.end(),
                                     *texture) == facts.supported_gl_textures.end()) {
                    facts.supported_gl_textures.push_back(*texture);
                }
            } else if (elements.size() == 1 && name == "uses-permission") {
                const auto* permission_name = FindAttribute(attributes, "name", kAndroidNamespace);
                const auto* permission = permission_name && permission_name->data_type == kStringType
                                             ? strings.Get(permission_name->data)
                                             : nullptr;
                const auto* max_sdk = FindAttribute(attributes, "maxSdkVersion", kAndroidNamespace);
                const bool excluded = max_sdk && max_sdk->data_type >= 0x10U && max_sdk->data_type <= 0x1fU &&
                                      max_sdk->data != 0 && std::bit_cast<std::int32_t>(max_sdk->data) < 19;
                if (!permission || excluded) {
                    elements.push_back(name);
                    cursor += chunk.size;
                    continue;
                }
                if (std::find(facts.requested_permissions.begin(), facts.requested_permissions.end(), *permission) ==
                    facts.requested_permissions.end()) {
                    facts.requested_permissions.push_back(*permission);
                }
            }
            elements.push_back(name);
        } else if (chunk.type == kEndElementType) {
            if (chunk.header_size < 16 || chunk.size - chunk.header_size < 8 || elements.empty()) {
                throw std::runtime_error("binary XML end element is invalid");
            }
            if (skip_depth) {
                elements.pop_back();
                if (elements.size() == *skip_depth) skip_depth.reset();
                cursor += chunk.size;
                continue;
            }
            // API19 tracks depth, not matching end-tag names.
            const auto& name = elements.back();
            if (name == "intent-filter" && elements.size() == 4 &&
                (current_component || current_service || current_receiver) && current_intent_filter.has_value()) {
                auto& filters = current_receiver  ? facts.receiver_components[*current_receiver].intent_filters
                                : current_service ? facts.service_components[*current_service].intent_filters
                                                  : facts.activity_components[*current_component].intent_filters;
                filters.push_back(std::move(*current_intent_filter));
                current_intent_filter.reset();
            } else if ((name == "activity" || name == "activity-alias") && elements.size() == 3 &&
                       current_component.has_value()) {
                auto& component = facts.activity_components[*current_component];
                if (!component.exported.has_value()) {
                    component.exported = !component.intent_filters.empty();
                }
                current_component.reset();
            } else if (name == "service" && elements.size() == 3) {
                current_service.reset();
            } else if (name == "receiver" && elements.size() == 3) {
                current_receiver.reset();
            } else if (name == "permission" && elements.size() == 2) {
                current_permission.reset();
            }
            elements.pop_back();
            if (elements.empty()) break;
        } else if (chunk.type == kResourceMapType) {
            // ResXMLTree uses the declared header and floors the word count.
            read_resource_map(cursor, chunk);
        } else if (chunk.type == kStartNamespaceType || chunk.type == kEndNamespaceType) {
            if (chunk.header_size < 16 || chunk.size - chunk.header_size < 8)
                throw std::runtime_error("binary XML namespace node is invalid");
        } else if (chunk.type == kTextType) {
            if (chunk.header_size < 16 || chunk.size - chunk.header_size < 12)
                throw std::runtime_error("binary XML text node is invalid");
        } else {
            // Unknown blocks are skipped by ResXMLParser::nextNode.
            if (chunk.header_size < 16) throw std::runtime_error("binary XML unknown node header is invalid");
        }
        cursor += chunk.size;
    }
    if (!saw_manifest || !elements.empty()) throw std::runtime_error("binary AndroidManifest structure is invalid");
    if (!ValidPackage(facts.package)) throw std::runtime_error("binary AndroidManifest package is invalid");
    std::unordered_set<std::string> registered_permissions;
    for (auto& definition : facts.defined_permissions) {
        definition.registered = (!definition.group || permission_groups.contains(*definition.group)) &&
                                registered_permissions.insert(definition.name).second;
    }
    try {
        facts.launcher_activity = ResolveLauncherComponent(facts).activity_class;
    } catch (const AndroidManifestStartupError& error) {
        if (error.Reason() != AndroidManifestStartupErrorReason::no_launcher) throw;
    }
    return facts;
}

AndroidManifestFacts ReadAndroidManifest(const std::span<const std::byte> apk_bytes, const ApkArchive& archive,
                                         const AndroidManifestResourceResolver& external_resources) {
    const auto manifest = ReadApkEntry(apk_bytes, archive, "AndroidManifest.xml");
    std::optional<ArscTable> table;
    const AndroidManifestResourceResolver resources =
        [&](const std::uint32_t id) -> std::optional<AndroidManifestResourceValue> {
        if ((id >> 24U) == 1U) return external_resources ? external_resources(id) : std::nullopt;
        if (!table) {
            const auto bytes = ReadApkEntry(apk_bytes, archive, "resources.arsc");
            table = ParseArsc({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        }
        const ArscEntry* best{};
        for (const auto& entry : table->entries) {
            // Package parsing uses the default configuration, fixed to API 19.
            if (entry.resource_id != id || entry.sdk_version > 19U || entry.orientation || entry.smallest_width_dp ||
                entry.screen_width_dp || entry.screen_height_dp || (entry.screen_layout & 0x0fU))
                continue;
            const auto density_better = [](std::uint16_t left, std::uint16_t right) {
                if (left == right) return false;
                if (left == 0xffffU) return true;
                if (right == 0xffffU) return false;
                int high = left ? left : 160;
                int low = right ? right : 160;
                bool left_high = true;
                if (low > high) {
                    std::swap(low, high);
                    left_high = false;
                }
                if (160 >= high) return left_high;
                if (low >= 160) return !left_high;
                return ((2 * low - 160) * high > 160 * 160) ? !left_high : left_high;
            };
            if (!best || (entry.density != best->density ? density_better(entry.density, best->density)
                                                         : entry.sdk_version > best->sdk_version))
                best = &entry;
        }
        if (!best) return std::nullopt;
        if (best->is_complex)
            throw std::runtime_error("binary AndroidManifest scalar resource is a bag: " + std::to_string(id));
        return AndroidManifestResourceValue{best->value_type, best->value_data, best->string_value};
    };
    return ParseAndroidBinaryManifest(manifest, resources);
}

}  // namespace ogplay::loader
