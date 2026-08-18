#include "ogplay/loader/apk_manifest.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
constexpr std::uint8_t kIntegerDecimalType = 0x10;
constexpr std::uint8_t kIntegerHexType = 0x11;
constexpr std::uint8_t kIntegerBooleanType = 0x12;
constexpr std::string_view kAndroidNamespace =
    "http://schemas.android.com/apk/res/android";
constexpr std::string_view kMainAction = "android.intent.action.MAIN";
constexpr std::string_view kLauncherCategory =
    "android.intent.category.LAUNCHER";

void RequireRange(const std::span<const std::byte> bytes, const std::size_t offset,
                  const std::size_t size, const std::string_view what) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw std::runtime_error(std::string(what) + " is outside binary AndroidManifest");
    }
}

std::uint16_t Read16(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 2, "binary XML field");
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(
                   std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U);
}

std::uint32_t Read32(const std::span<const std::byte> bytes, const std::size_t offset) {
    RequireRange(bytes, offset, 4, "binary XML field");
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

struct Chunk final {
    std::uint16_t type{};
    std::uint16_t header_size{};
    std::uint32_t size{};
};

Chunk ReadChunk(const std::span<const std::byte> bytes, const std::size_t offset,
                const std::string_view what) {
    RequireRange(bytes, offset, 8, what);
    const Chunk chunk{Read16(bytes, offset), Read16(bytes, offset + 2),
                      Read32(bytes, offset + 4)};
    if (chunk.header_size < 8 || chunk.size < chunk.header_size) {
        throw std::runtime_error(std::string(what) + " has an invalid chunk header");
    }
    RequireRange(bytes, offset, chunk.size, what);
    return chunk;
}

std::size_t ReadLength8(const std::span<const std::byte> bytes, std::size_t& cursor,
                        const std::size_t end) {
    if (cursor >= end) throw std::runtime_error("binary XML UTF-8 length is truncated");
    const auto first = std::to_integer<std::uint8_t>(bytes[cursor++]);
    if ((first & 0x80U) == 0) return first;
    if (cursor >= end) throw std::runtime_error("binary XML UTF-8 length is truncated");
    return (static_cast<std::size_t>(first & 0x7fU) << 8U) |
           std::to_integer<std::uint8_t>(bytes[cursor++]);
}

std::size_t ReadLength16(const std::span<const std::byte> bytes, std::size_t& cursor,
                         const std::size_t end) {
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

void AppendUtf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7fU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffU) {
        output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else if (code_point <= 0xffffU) {
        output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
    }
}

std::size_t ValidateUtf8(const std::string_view text) {
    std::size_t utf16_length{};
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[index]);
        std::uint32_t code_point{};
        std::size_t count{};
        if (first <= 0x7fU) {
            code_point = first;
            count = 1;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            code_point = first & 0x1fU;
            count = 2;
        } else if (first >= 0xe0U && first <= 0xefU) {
            code_point = first & 0x0fU;
            count = 3;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = first & 0x07U;
            count = 4;
        } else {
            throw std::runtime_error("binary XML string pool contains invalid UTF-8");
        }
        if (count > text.size() - index) {
            throw std::runtime_error("binary XML string pool contains truncated UTF-8");
        }
        for (std::size_t continuation = 1; continuation < count; ++continuation) {
            const auto byte = static_cast<std::uint8_t>(text[index + continuation]);
            if ((byte & 0xc0U) != 0x80U) {
                throw std::runtime_error("binary XML string pool contains invalid UTF-8");
            }
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if ((count == 3 && code_point < 0x800U) ||
            (count == 4 && code_point < 0x10000U) || code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            throw std::runtime_error("binary XML string pool contains non-canonical UTF-8");
        }
        ++utf16_length;
        if (code_point > 0xffffU) ++utf16_length;
        index += count;
    }
    return utf16_length;
}

std::string ReadUtf8String(const std::span<const std::byte> bytes, std::size_t cursor,
                           const std::size_t end) {
    const auto expected_utf16 = ReadLength8(bytes, cursor, end);
    const auto byte_length = ReadLength8(bytes, cursor, end);
    if (cursor > end || byte_length >= end - cursor) {
        throw std::runtime_error("binary XML UTF-8 string is truncated");
    }
    std::string result;
    result.reserve(byte_length);
    for (std::size_t index = 0; index < byte_length; ++index) {
        const auto value = std::to_integer<std::uint8_t>(bytes[cursor + index]);
        if (value == 0) throw std::runtime_error("binary XML UTF-8 string contains NUL");
        result.push_back(static_cast<char>(value));
    }
    if (bytes[cursor + byte_length] != std::byte{0}) {
        throw std::runtime_error("binary XML UTF-8 string is not terminated");
    }
    if (ValidateUtf8(result) != expected_utf16) {
        throw std::runtime_error("binary XML UTF-8 string length disagrees with content");
    }
    return result;
}

std::string ReadUtf16String(const std::span<const std::byte> bytes, std::size_t cursor,
                            const std::size_t end) {
    const auto length = ReadLength16(bytes, cursor, end);
    if (length > (end - cursor) / 2U || length == (end - cursor) / 2U) {
        throw std::runtime_error("binary XML UTF-16 string is truncated");
    }
    std::string result;
    for (std::size_t index = 0; index < length; ++index) {
        const auto unit = Read16(bytes, cursor + index * 2U);
        std::uint32_t code_point = unit;
        if (unit >= 0xd800U && unit <= 0xdbffU) {
            if (++index >= length) {
                throw std::runtime_error("binary XML UTF-16 string has a lone surrogate");
            }
            const auto low = Read16(bytes, cursor + index * 2U);
            if (low < 0xdc00U || low > 0xdfffU) {
                throw std::runtime_error("binary XML UTF-16 string has a lone surrogate");
            }
            code_point = 0x10000U + ((unit - 0xd800U) << 10U) + (low - 0xdc00U);
        } else if (unit >= 0xdc00U && unit <= 0xdfffU) {
            throw std::runtime_error("binary XML UTF-16 string has a lone surrogate");
        }
        if (code_point == 0) throw std::runtime_error("binary XML UTF-16 string contains NUL");
        AppendUtf8(result, code_point);
    }
    if (Read16(bytes, cursor + length * 2U) != 0) {
        throw std::runtime_error("binary XML UTF-16 string is not terminated");
    }
    return result;
}

std::vector<std::string> ReadStringPool(const std::span<const std::byte> bytes,
                                        const std::size_t offset, const Chunk& chunk) {
    if (chunk.type != kStringPoolType || chunk.header_size != 28) {
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
    if (table_bytes > chunk.size - chunk.header_size ||
        strings_start < chunk.header_size + table_bytes || strings_start >= chunk.size) {
        throw std::runtime_error("binary XML string pool offsets are invalid");
    }
    const auto strings_end = styles_start == 0 ? static_cast<std::size_t>(chunk.size)
                                                : styles_start;
    if (strings_end <= strings_start || strings_end > chunk.size) {
        throw std::runtime_error("binary XML string pool data range is invalid");
    }

    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto relative = static_cast<std::size_t>(
            Read32(bytes, offset + chunk.header_size + index * 4U));
        if (relative >= strings_end - strings_start) {
            throw std::runtime_error("binary XML string offset is outside string data");
        }
        const auto cursor = offset + strings_start + relative;
        const auto end = offset + strings_end;
        result.push_back((flags & kUtf8Flag) != 0
                             ? ReadUtf8String(bytes, cursor, end)
                             : ReadUtf16String(bytes, cursor, end));
    }
    return result;
}

const std::string& StringAt(const std::vector<std::string>& strings,
                            const std::uint32_t index, const std::string_view field) {
    if (index >= strings.size()) {
        throw std::runtime_error(std::string(field) + " string index is invalid");
    }
    return strings[index];
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
            if (first) return false;
            first = true;
            ++components;
        } else {
            if (!IsPackageCharacter(value, first)) return false;
            first = false;
        }
    }
    return !first && components >= 2;
}

struct Attribute final {
    std::string_view name;
    std::optional<std::string_view> namespace_uri;
    std::uint32_t raw_value{kNoIndex};
    std::uint8_t data_type{};
    std::uint32_t data{};
};

std::vector<Attribute> ReadAttributes(const std::span<const std::byte> bytes,
                                      const std::size_t offset, const Chunk& chunk,
                                      const std::vector<std::string>& strings) {
    if (chunk.header_size != 16 || chunk.size < 36) {
        throw std::runtime_error("binary XML start element header is invalid");
    }
    const auto extension = offset + chunk.header_size;
    const auto attribute_start = static_cast<std::size_t>(Read16(bytes, extension + 8));
    const auto attribute_size = static_cast<std::size_t>(Read16(bytes, extension + 10));
    const auto attribute_count = static_cast<std::size_t>(Read16(bytes, extension + 12));
    const auto extension_size = static_cast<std::size_t>(chunk.size - chunk.header_size);
    if (attribute_start < 20 || attribute_size < 20 ||
        attribute_start > extension_size ||
        attribute_count > (extension_size - attribute_start) / attribute_size) {
        throw std::runtime_error("binary XML attribute table is invalid");
    }
    std::vector<Attribute> result;
    result.reserve(attribute_count);
    for (std::size_t index = 0; index < attribute_count; ++index) {
        const auto attribute = extension + attribute_start + index * attribute_size;
        if (Read16(bytes, attribute + 12) != 8 ||
            std::to_integer<std::uint8_t>(bytes[attribute + 14]) != 0) {
            throw std::runtime_error("binary XML typed attribute value is invalid");
        }
        const auto namespace_index = Read32(bytes, attribute);
        const auto raw_value = Read32(bytes, attribute + 8);
        const auto data_type = std::to_integer<std::uint8_t>(bytes[attribute + 15]);
        const auto data = Read32(bytes, attribute + 16);
        const auto namespace_uri = namespace_index == kNoIndex
                                       ? std::optional<std::string_view>{}
                                       : StringAt(strings, namespace_index,
                                                  "binary XML attribute namespace");
        if (raw_value != kNoIndex) {
            static_cast<void>(StringAt(strings, raw_value,
                                       "binary XML raw attribute value"));
        }
        if (data_type == kStringType) {
            static_cast<void>(StringAt(strings, data, "binary XML typed string value"));
        }
        result.push_back({StringAt(strings, Read32(bytes, attribute + 4),
                                   "binary XML attribute name"),
                          namespace_uri, raw_value,
                          data_type, data});
    }
    return result;
}

const Attribute* FindAttribute(const std::vector<Attribute>& attributes,
                               const std::string_view name,
                               const std::optional<std::string_view> namespace_uri) {
    const Attribute* result{};
    for (const auto& attribute : attributes) {
        if (attribute.name != name || attribute.namespace_uri != namespace_uri) continue;
        if (result != nullptr) {
            throw std::runtime_error("binary AndroidManifest contains duplicate attribute " +
                                     std::string(name));
        }
        result = &attribute;
    }
    return result;
}

std::string ReadStringAttribute(const Attribute& attribute,
                                const std::vector<std::string>& strings,
                                const std::string_view name) {
    const auto index = attribute.raw_value != kNoIndex
                           ? attribute.raw_value
                           : attribute.data_type == kStringType ? attribute.data : kNoIndex;
    if (index == kNoIndex) {
        throw std::runtime_error("binary AndroidManifest attribute " + std::string(name) +
                                 " is not a string");
    }
    return StringAt(strings, index, "binary AndroidManifest attribute");
}

std::uint32_t ReadIntegerAttribute(const Attribute& attribute,
                                   const std::string_view name) {
    if (attribute.data_type != kIntegerDecimalType && attribute.data_type != kIntegerHexType) {
        throw std::runtime_error("binary AndroidManifest attribute " + std::string(name) +
                                 " is not an integer");
    }
    return attribute.data;
}

std::uint32_t ReadReferenceAttribute(const Attribute& attribute,
                                     const std::string_view name) {
    if (attribute.data_type != kReferenceType || attribute.data == 0) {
        throw std::runtime_error("binary AndroidManifest attribute " +
                                 std::string(name) +
                                 " is not a resource reference");
    }
    return attribute.data;
}

bool ReadBooleanAttribute(const Attribute& attribute, const std::string_view name) {
    if (attribute.data_type != kIntegerBooleanType || attribute.data > 1U) {
        throw AndroidManifestStartupError(
            AndroidManifestStartupErrorReason::invalid_enabled,
            "binary AndroidManifest attribute " + std::string(name) +
                " is not a boolean");
    }
    return attribute.data != 0;
}

void SetOnce(std::optional<std::uint32_t>& destination, const std::uint32_t value,
             const std::string_view name) {
    if (destination.has_value()) {
        throw std::runtime_error("binary AndroidManifest contains duplicate " +
                                 std::string(name));
    }
    destination = value;
}

}  // namespace

AndroidManifestStartupError::AndroidManifestStartupError(
    const AndroidManifestStartupErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

AndroidManifestStartupErrorReason AndroidManifestStartupError::Reason() const noexcept {
    return reason_;
}

std::string NormalizeAndroidManifestClassName(const std::string_view package,
                                              const std::string_view class_name) {
    if (class_name.empty()) {
        throw AndroidManifestStartupError(
            AndroidManifestStartupErrorReason::invalid_class_name,
            "binary AndroidManifest contains an empty class name");
    }
    if (class_name.front() == '.') return std::string(package) + std::string(class_name);
    if (class_name.find('.') == std::string_view::npos) {
        return std::string(package) + "." + std::string(class_name);
    }
    if (class_name.front() >= 'a' && class_name.front() <= 'z') {
        return std::string(class_name);
    }
    throw AndroidManifestStartupError(
        AndroidManifestStartupErrorReason::invalid_class_name,
        "binary AndroidManifest contains invalid class name " +
            std::string(class_name));
}

AndroidManifestLauncherComponent ResolveLauncherComponent(
    const AndroidManifestFacts& facts) {
    const auto has_value = [](const std::vector<std::string>& values,
                              const std::string_view expected) {
        return std::find(values.begin(), values.end(), expected) != values.end();
    };
    for (const auto& component : facts.activity_components) {
        if (!component.enabled) continue;
        const auto launcher = std::any_of(
            component.intent_filters.begin(), component.intent_filters.end(),
            [&](const AndroidManifestIntentFilter& filter) {
                return has_value(filter.actions, kMainAction) &&
                       has_value(filter.categories, kLauncherCategory);
            });
        if (!launcher) continue;

        if (component.kind == AndroidManifestComponentKind::activity) {
            return {component.name, component.name, false};
        }
        const auto target = std::find_if(
            facts.activity_components.begin(), facts.activity_components.end(),
            [&](const AndroidManifestActivityComponent& candidate) {
                return candidate.kind == AndroidManifestComponentKind::activity &&
                       component.target_activity.has_value() &&
                       candidate.name == *component.target_activity;
            });
        if (target != facts.activity_components.end()) {
            return {component.name, target->name, true};
        }
    }
    throw AndroidManifestStartupError(
        AndroidManifestStartupErrorReason::no_launcher,
        "binary AndroidManifest has no enabled MAIN/LAUNCHER activity");
}

AndroidManifestFacts ParseAndroidBinaryManifest(const std::span<const std::byte> bytes) {
    const auto xml = ReadChunk(bytes, 0, "binary AndroidManifest");
    if (xml.type != kXmlType || xml.header_size != 8 || xml.size != bytes.size()) {
        throw std::runtime_error("AndroidManifest.xml is not one complete binary XML document");
    }
    std::size_t cursor = xml.header_size;
    const auto string_chunk = ReadChunk(bytes, cursor, "binary XML string pool");
    const auto strings = ReadStringPool(bytes, cursor, string_chunk);
    cursor += string_chunk.size;

    AndroidManifestFacts facts;
    std::vector<std::string> elements;
    bool saw_manifest{};
    bool saw_application{};
    bool saw_uses_sdk{};
    std::optional<std::size_t> current_component;
    std::optional<AndroidManifestIntentFilter> current_intent_filter;
    while (cursor < bytes.size()) {
        const auto chunk = ReadChunk(bytes, cursor, "binary XML node");
        if (chunk.type == kStartElementType) {
            const auto attributes = ReadAttributes(bytes, cursor, chunk, strings);
            const auto namespace_index = Read32(bytes, cursor + chunk.header_size);
            if (namespace_index != kNoIndex) {
                static_cast<void>(StringAt(strings, namespace_index,
                                           "binary XML element namespace"));
            }
            const auto name = StringAt(strings, Read32(bytes, cursor + chunk.header_size + 4),
                                       "binary XML element name");
            if (elements.empty()) {
                if (saw_manifest || name != "manifest") {
                    throw std::runtime_error("binary AndroidManifest root element is invalid");
                }
                saw_manifest = true;
                const auto* package = FindAttribute(attributes, "package", std::nullopt);
                const auto* version_code =
                    FindAttribute(attributes, "versionCode", kAndroidNamespace);
                if (package == nullptr || version_code == nullptr) {
                    throw std::runtime_error(
                        "binary AndroidManifest is missing package or versionCode");
                }
                facts.package = ReadStringAttribute(*package, strings, "package");
                facts.version_code = ReadIntegerAttribute(*version_code, "versionCode");
                if (const auto* version_name =
                        FindAttribute(attributes, "versionName", kAndroidNamespace)) {
                    facts.version_name =
                        ReadStringAttribute(*version_name, strings, "versionName");
                }
            } else if (name == "application" && elements.size() == 1) {
                if (saw_application) {
                    throw std::runtime_error(
                        "binary AndroidManifest contains duplicate application");
                }
                saw_application = true;
                if (const auto* icon =
                        FindAttribute(attributes, "icon", kAndroidNamespace)) {
                    facts.application_icon =
                        ReadReferenceAttribute(*icon, "application icon");
                }
                if (const auto* label =
                        FindAttribute(attributes, "label", kAndroidNamespace)) {
                    if (label->data_type == kReferenceType) {
                        facts.application_label =
                            ReadReferenceAttribute(*label, "application label");
                    } else if (label->data_type == kStringType) {
                        facts.application_label = ReadStringAttribute(
                            *label, strings, "application label");
                    } else {
                        throw std::runtime_error(
                            "binary AndroidManifest application label is neither a "
                            "resource reference nor a string");
                    }
                }
                if (const auto* application_name =
                        FindAttribute(attributes, "name", kAndroidNamespace)) {
                    facts.application_class = NormalizeAndroidManifestClassName(
                        facts.package,
                        ReadStringAttribute(*application_name, strings,
                                            "application name"));
                }
            } else if ((name == "activity" || name == "activity-alias") &&
                       elements.size() == 2 && elements[1] == "application") {
                const auto* component_name =
                    FindAttribute(attributes, "name", kAndroidNamespace);
                if (component_name == nullptr) {
                    throw AndroidManifestStartupError(
                        AndroidManifestStartupErrorReason::missing_component_name,
                        "binary AndroidManifest " + name +
                            " does not specify android:name");
                }
                AndroidManifestActivityComponent component;
                component.kind = name == "activity"
                                     ? AndroidManifestComponentKind::activity
                                     : AndroidManifestComponentKind::activity_alias;
                component.name = NormalizeAndroidManifestClassName(
                    facts.package,
                    ReadStringAttribute(*component_name, strings, name + " name"));
                if (const auto duplicate = std::find_if(
                        facts.activity_components.begin(),
                        facts.activity_components.end(),
                        [&](const AndroidManifestActivityComponent& candidate) {
                            return candidate.name == component.name;
                        }); duplicate != facts.activity_components.end()) {
                    throw AndroidManifestStartupError(
                        AndroidManifestStartupErrorReason::duplicate_component,
                        "binary AndroidManifest contains duplicate activity component " +
                            component.name);
                }
                if (const auto* enabled =
                        FindAttribute(attributes, "enabled", kAndroidNamespace)) {
                    component.enabled = ReadBooleanAttribute(*enabled, name + " enabled");
                }
                if (component.kind == AndroidManifestComponentKind::activity_alias) {
                    const auto* target =
                        FindAttribute(attributes, "targetActivity", kAndroidNamespace);
                    if (target == nullptr) {
                        throw AndroidManifestStartupError(
                            AndroidManifestStartupErrorReason::missing_alias_target,
                            "binary AndroidManifest activity-alias does not specify "
                            "android:targetActivity");
                    }
                    component.target_activity = NormalizeAndroidManifestClassName(
                        facts.package,
                        ReadStringAttribute(*target, strings,
                                            "activity-alias targetActivity"));
                    const auto target_activity = std::find_if(
                        facts.activity_components.begin(),
                        facts.activity_components.end(),
                        [&](const AndroidManifestActivityComponent& candidate) {
                            return candidate.kind == AndroidManifestComponentKind::activity &&
                                   candidate.name == *component.target_activity;
                        });
                    if (target_activity == facts.activity_components.end()) {
                        throw AndroidManifestStartupError(
                            AndroidManifestStartupErrorReason::alias_target_not_found,
                            "binary AndroidManifest activity-alias target " +
                                *component.target_activity +
                                " was not declared before the alias");
                    }
                }
                facts.activity_components.push_back(std::move(component));
                current_component = facts.activity_components.size() - 1U;
                current_intent_filter.reset();
            } else if (name == "intent-filter" && current_component.has_value() &&
                       !elements.empty() &&
                       (elements.back() == "activity" ||
                        elements.back() == "activity-alias")) {
                current_intent_filter.emplace();
            } else if (name == "action" && current_component.has_value() &&
                       current_intent_filter.has_value() &&
                       !elements.empty() &&
                       elements.back() == "intent-filter") {
                if (const auto* action_name =
                        FindAttribute(attributes, "name", kAndroidNamespace)) {
                    current_intent_filter->actions.push_back(ReadStringAttribute(
                        *action_name, strings, "action name"));
                }
            } else if (name == "category" && current_component.has_value() &&
                       current_intent_filter.has_value() &&
                       !elements.empty() &&
                       elements.back() == "intent-filter") {
                if (const auto* category_name =
                        FindAttribute(attributes, "name", kAndroidNamespace)) {
                    current_intent_filter->categories.push_back(ReadStringAttribute(
                        *category_name, strings, "category name"));
                }
            } else if (elements.size() == 1 && name == "uses-sdk") {
                if (saw_uses_sdk) {
                    throw std::runtime_error(
                        "binary AndroidManifest contains duplicate uses-sdk");
                }
                saw_uses_sdk = true;
                if (const auto* min_sdk =
                        FindAttribute(attributes, "minSdkVersion", kAndroidNamespace)) {
                    SetOnce(facts.min_sdk, ReadIntegerAttribute(*min_sdk, "minSdkVersion"),
                            "minSdkVersion");
                }
                if (const auto* target_sdk =
                        FindAttribute(attributes, "targetSdkVersion", kAndroidNamespace)) {
                    SetOnce(facts.target_sdk,
                            ReadIntegerAttribute(*target_sdk, "targetSdkVersion"),
                            "targetSdkVersion");
                }
            }
            elements.push_back(name);
        } else if (chunk.type == kEndElementType) {
            if (chunk.header_size != 16 || chunk.size < 24 || elements.empty()) {
                throw std::runtime_error("binary XML end element is invalid");
            }
            const auto namespace_index = Read32(bytes, cursor + chunk.header_size);
            if (namespace_index != kNoIndex) {
                static_cast<void>(StringAt(strings, namespace_index,
                                           "binary XML end element namespace"));
            }
            const auto& name = StringAt(strings, Read32(bytes, cursor + chunk.header_size + 4),
                                        "binary XML end element name");
            if (name != elements.back()) {
                throw std::runtime_error("binary XML element nesting is invalid");
            }
            if (name == "intent-filter" && current_component.has_value() &&
                current_intent_filter.has_value()) {
                facts.activity_components[*current_component].intent_filters.push_back(
                    std::move(*current_intent_filter));
                current_intent_filter.reset();
            } else if ((name == "activity" || name == "activity-alias") &&
                       current_component.has_value()) {
                current_component.reset();
            }
            elements.pop_back();
        } else if (chunk.type == kResourceMapType) {
            if (chunk.header_size != 8 || (chunk.size - chunk.header_size) % 4U != 0) {
                throw std::runtime_error("binary XML resource map is invalid");
            }
        } else if (chunk.type == kStartNamespaceType || chunk.type == kEndNamespaceType) {
            if (chunk.header_size != 16 || chunk.size != 24) {
                throw std::runtime_error("binary XML namespace node is invalid");
            }
            for (const auto index : {Read32(bytes, cursor + 16),
                                     Read32(bytes, cursor + 20)}) {
                if (index != kNoIndex) {
                    static_cast<void>(StringAt(strings, index,
                                               "binary XML namespace string"));
                }
            }
        } else if (chunk.type == kTextType) {
            if (chunk.header_size != 16 || chunk.size < 28) {
                throw std::runtime_error("binary XML text node is invalid");
            }
            static_cast<void>(StringAt(strings, Read32(bytes, cursor + 16),
                                       "binary XML text"));
            if (Read16(bytes, cursor + 20) != 8 ||
                std::to_integer<std::uint8_t>(bytes[cursor + 22]) != 0) {
                throw std::runtime_error("binary XML typed text value is invalid");
            }
        } else {
            throw std::runtime_error("binary AndroidManifest contains an unsupported chunk");
        }
        cursor += chunk.size;
    }
    if (!saw_manifest || !elements.empty() || !ValidPackage(facts.package) ||
        facts.version_code == 0 ||
        (facts.version_name.has_value() && facts.version_name->empty()) ||
        (facts.min_sdk.has_value() && *facts.min_sdk == 0) ||
        (facts.target_sdk.has_value() && *facts.target_sdk == 0)) {
        throw std::runtime_error("binary AndroidManifest identity or structure is invalid");
    }
    try {
        facts.launcher_activity = ResolveLauncherComponent(facts).activity_class;
    } catch (const AndroidManifestStartupError& error) {
        if (error.Reason() != AndroidManifestStartupErrorReason::no_launcher) throw;
    }
    return facts;
}

AndroidManifestFacts ReadAndroidManifest(const std::span<const std::byte> apk_bytes,
                                         const ApkArchive& archive) {
    const auto manifest = ReadApkEntry(apk_bytes, archive, "AndroidManifest.xml");
    return ParseAndroidBinaryManifest(manifest);
}

}  // namespace ogplay::loader
