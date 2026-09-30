#include "ogplay/loader/dex.h"

#include "dex_uleb128.h"
#include <algorithm>
#include <bit>
#include <iterator>
#include <limits>
#include <map>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kDexHeaderSize = 0x70;
constexpr std::uint32_t kDexEndianConstant = 0x12345678;

[[noreturn]] void Fail(const DexErrorReason reason, const std::size_t offset,
                       std::string message) {
    throw DexError(reason, offset, std::move(message));
}

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint16_t U16(const std::size_t offset) const {
        Require(offset, 2);
        const auto value = static_cast<std::uint32_t>(bytes_[offset]) |
                           static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U;
        return static_cast<std::uint16_t>(value);
    }

    [[nodiscard]] std::uint8_t U8(const std::size_t offset) const {
        Require(offset, 1);
        return bytes_[offset];
    }

    [[nodiscard]] std::uint32_t U32(const std::size_t offset) const {
        Require(offset, 4);
        return static_cast<std::uint32_t>(bytes_[offset]) |
               static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U |
               static_cast<std::uint32_t>(bytes_[offset + 2]) << 16U |
               static_cast<std::uint32_t>(bytes_[offset + 3]) << 24U;
    }

    void Require(const std::size_t offset, const std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            Fail(DexErrorReason::truncated, offset, "DEX input is truncated");
        }
    }

    [[nodiscard]] std::uint32_t Uleb128(std::size_t& offset) const {
        return detail::ReadUleb128(bytes_, offset,
                                   {"DEX input is truncated",
                                    "DEX ULEB128 exceeds 32 bits",
                                    "DEX ULEB128 is not minimally encoded",
                                    "DEX ULEB128 is unterminated"});
    }

private:
    std::span<const std::uint8_t> bytes_;
};

[[nodiscard]] std::string NarrowString(const std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(result),
                   [](const char16_t unit) { return static_cast<char>(unit); });
    return result;
}

void RequireRange(const std::uint32_t offset, const std::uint32_t count,
                  const std::uint32_t item_size,
                  const std::size_t file_size, const char* name) {
    if ((count == 0) != (offset == 0)) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " size/offset pair is inconsistent");
    }
    if (count == 0) return;
    if ((offset & 3U) != 0) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " offset is not aligned");
    }
    const auto bytes = static_cast<std::uint64_t>(count) * item_size;
    if (offset > file_size || bytes > file_size - offset) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " range exceeds file");
    }
}

[[nodiscard]] std::optional<std::uint32_t> FixedItemSize(
    const std::uint16_t type) {
    switch (static_cast<DexMapItemType>(type)) {
    case DexMapItemType::header:
        return 0x70;
    case DexMapItemType::string_id:
    case DexMapItemType::type_id:
    case DexMapItemType::call_site_id:
        return 4;
    case DexMapItemType::proto_id:
        return 12;
    case DexMapItemType::field_id:
    case DexMapItemType::method_id:
    case DexMapItemType::method_handle:
        return 8;
    case DexMapItemType::class_def:
        return 32;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] DexHeader ReadHeader(const Reader& reader,
                                   const std::span<const std::uint8_t> bytes) {
    reader.Require(0, kDexHeaderSize);
    if (bytes[0] != 'd' || bytes[1] != 'e' || bytes[2] != 'x' ||
        bytes[3] != '\n' || bytes[7] != 0) {
        Fail(DexErrorReason::invalid_magic, 0, "DEX magic is invalid");
    }
    const std::string version(bytes.begin() + 4, bytes.begin() + 7);
    if (version < "035" || version > "040") {
        Fail(DexErrorReason::unsupported_version, 4,
             "DEX version is outside the supported 035..040 range");
    }

    DexHeader header;
    header.version = version;
    header.checksum = reader.U32(8);
    std::copy_n(bytes.begin() + 12, header.signature.size(),
                header.signature.begin());
    header.file_size = reader.U32(32);
    header.header_size = reader.U32(36);
    header.endian_tag = reader.U32(40);
    header.link_size = reader.U32(44);
    header.link_offset = reader.U32(48);
    header.map_offset = reader.U32(52);
    header.string_ids_size = reader.U32(56);
    header.string_ids_offset = reader.U32(60);
    header.type_ids_size = reader.U32(64);
    header.type_ids_offset = reader.U32(68);
    header.proto_ids_size = reader.U32(72);
    header.proto_ids_offset = reader.U32(76);
    header.field_ids_size = reader.U32(80);
    header.field_ids_offset = reader.U32(84);
    header.method_ids_size = reader.U32(88);
    header.method_ids_offset = reader.U32(92);
    header.class_defs_size = reader.U32(96);
    header.class_defs_offset = reader.U32(100);
    header.data_size = reader.U32(104);
    header.data_offset = reader.U32(108);
    return header;
}

void ValidateHeader(const DexHeader& header, const std::size_t file_size) {
    if (header.file_size != file_size || header.header_size != kDexHeaderSize) {
        Fail(DexErrorReason::invalid_header, 32,
             "DEX file or header size is invalid");
    }
    if (header.endian_tag != kDexEndianConstant) {
        Fail(DexErrorReason::invalid_endian, 40,
             "DEX reverse or unknown endian tag is unsupported");
    }
    RequireRange(header.link_offset, header.link_size, 1, file_size, "link");
    RequireRange(header.string_ids_offset, header.string_ids_size, 4,
                 file_size, "string_ids");
    RequireRange(header.type_ids_offset, header.type_ids_size, 4,
                 file_size, "type_ids");
    RequireRange(header.proto_ids_offset, header.proto_ids_size, 12,
                 file_size, "proto_ids");
    RequireRange(header.field_ids_offset, header.field_ids_size, 8,
                 file_size, "field_ids");
    RequireRange(header.method_ids_offset, header.method_ids_size, 8,
                 file_size, "method_ids");
    RequireRange(header.class_defs_offset, header.class_defs_size, 32,
                 file_size, "class_defs");
    RequireRange(header.data_offset, header.data_size, 1, file_size, "data");
    if (header.data_size == 0 || header.map_offset < header.data_offset ||
        header.map_offset >= file_size || (header.map_offset & 3U) != 0) {
        Fail(DexErrorReason::invalid_header, 52,
             "DEX map must be aligned inside the data section");
    }
    const auto data_end = static_cast<std::uint64_t>(header.data_offset) +
                          header.data_size;
    if (data_end != file_size || header.map_offset >= data_end) {
        Fail(DexErrorReason::invalid_range, header.data_offset,
             "DEX data section must end at file boundary");
    }
}

[[nodiscard]] std::vector<DexMapItem> ReadMap(
    const Reader& reader, const DexHeader& header,
    const std::size_t file_size) {
    const auto count = reader.U32(header.map_offset);
    if (count == 0 || count >
                          (std::numeric_limits<std::uint32_t>::max() - 4U) /
                              12U) {
        Fail(DexErrorReason::invalid_map, header.map_offset,
             "DEX map count is invalid");
    }
    const auto map_bytes = 4ULL + static_cast<std::uint64_t>(count) * 12ULL;
    if (map_bytes > file_size - header.map_offset) {
        Fail(DexErrorReason::invalid_map, header.map_offset,
             "DEX map range exceeds file");
    }

    std::vector<DexMapItem> items;
    items.reserve(count);
    std::map<std::uint16_t, std::size_t> seen;
    std::uint32_t previous_offset{};
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto at = static_cast<std::size_t>(header.map_offset) + 4ULL +
                        static_cast<std::size_t>(index) * 12ULL;
        const auto type = reader.U16(at);
        const auto unused = reader.U16(at + 2);
        const auto size = reader.U32(at + 4);
        const auto offset = reader.U32(at + 8);
        if (unused != 0 || size == 0 || !seen.emplace(type, at).second ||
            (index != 0 && offset <= previous_offset) || offset >= file_size) {
            Fail(DexErrorReason::invalid_map, at,
                 "DEX map item is duplicate, unordered or malformed");
        }
        if (const auto fixed = FixedItemSize(type); fixed.has_value()) {
            const auto bytes = static_cast<std::uint64_t>(size) * *fixed;
            if ((offset & 3U) != 0 || bytes > file_size - offset) {
                Fail(DexErrorReason::invalid_map, at,
                     "DEX fixed map item range is invalid");
            }
        }
        items.push_back({type, size, offset});
        previous_offset = offset;
    }
    return items;
}

void RequireMapMatch(const DexImage& image, const DexMapItemType type,
                     const std::uint32_t size, const std::uint32_t offset) {
    const auto found = image.FindMapItem(type);
    if (size == 0) {
        if (found.has_value()) {
            Fail(DexErrorReason::invalid_map, found->offset,
                 "empty DEX header section appears in map");
        }
        return;
    }
    if (!found.has_value() || found->size != size || found->offset != offset) {
        Fail(DexErrorReason::invalid_map, offset,
             "DEX header and map section facts disagree");
    }
}

void ValidateMap(const DexImage& image) {
    RequireMapMatch(image, DexMapItemType::header, 1, 0);
    RequireMapMatch(image, DexMapItemType::string_id,
                    image.header.string_ids_size,
                    image.header.string_ids_offset);
    RequireMapMatch(image, DexMapItemType::type_id,
                    image.header.type_ids_size,
                    image.header.type_ids_offset);
    RequireMapMatch(image, DexMapItemType::proto_id,
                    image.header.proto_ids_size,
                    image.header.proto_ids_offset);
    RequireMapMatch(image, DexMapItemType::field_id,
                    image.header.field_ids_size,
                    image.header.field_ids_offset);
    RequireMapMatch(image, DexMapItemType::method_id,
                    image.header.method_ids_size,
                    image.header.method_ids_offset);
    RequireMapMatch(image, DexMapItemType::class_def,
                    image.header.class_defs_size,
                    image.header.class_defs_offset);
    RequireMapMatch(image, DexMapItemType::map_list, 1,
                    image.header.map_offset);
}

[[nodiscard]] DexString ReadString(const Reader& reader,
                                   const DexHeader& header,
                                   const std::uint32_t data_offset) {
    if (data_offset < header.data_offset || data_offset >= header.file_size) {
        Fail(DexErrorReason::invalid_range, data_offset,
             "DEX string_data offset is outside data section");
    }
    std::size_t cursor = data_offset;
    const auto expected_units = reader.Uleb128(cursor);
    std::u16string value;
    value.reserve(expected_units);
    while (true) {
        const auto first = reader.U8(cursor++);
        if (first == 0) break;
        if (first <= 0x7fU) {
            value.push_back(static_cast<char16_t>(first));
            continue;
        }
        if ((first & 0xe0U) == 0xc0U) {
            const auto second = reader.U8(cursor++);
            if ((second & 0xc0U) != 0x80U) {
                Fail(DexErrorReason::invalid_string, cursor - 1,
                     "DEX Modified UTF-8 continuation is invalid");
            }
            const auto unit_value =
                ((static_cast<std::uint32_t>(first) & 0x1fU) << 6U) |
                (static_cast<std::uint32_t>(second) & 0x3fU);
            const auto unit = static_cast<std::uint16_t>(unit_value);
            if (unit != 0 && unit < 0x80U) {
                Fail(DexErrorReason::invalid_string, cursor - 2,
                     "DEX Modified UTF-8 has an overlong sequence");
            }
            value.push_back(static_cast<char16_t>(unit));
            continue;
        }
        if ((first & 0xf0U) == 0xe0U) {
            const auto second = reader.U8(cursor++);
            const auto third = reader.U8(cursor++);
            if ((second & 0xc0U) != 0x80U || (third & 0xc0U) != 0x80U) {
                Fail(DexErrorReason::invalid_string, cursor - 2,
                     "DEX Modified UTF-8 continuation is invalid");
            }
            const auto unit_value =
                ((static_cast<std::uint32_t>(first) & 0x0fU) << 12U) |
                ((static_cast<std::uint32_t>(second) & 0x3fU) << 6U) |
                (static_cast<std::uint32_t>(third) & 0x3fU);
            const auto unit = static_cast<std::uint16_t>(unit_value);
            if (unit < 0x800U) {
                Fail(DexErrorReason::invalid_string, cursor - 3,
                     "DEX Modified UTF-8 has an overlong sequence");
            }
            value.push_back(static_cast<char16_t>(unit));
            continue;
        }
        Fail(DexErrorReason::invalid_string, cursor - 1,
             "DEX Modified UTF-8 lead byte is invalid");
    }
    if (value.size() != expected_units) {
        Fail(DexErrorReason::invalid_string, data_offset,
             "DEX string utf16_size does not match decoded data");
    }
    return {data_offset, std::move(value)};
}

[[nodiscard]] std::string RequireAscii(const DexString& string,
                                       const char* purpose) {
    std::string result;
    result.reserve(string.value.size());
    for (const auto unit : string.value) {
        if (unit == 0 || unit > 0x7f) {
            Fail(DexErrorReason::invalid_descriptor, string.data_offset,
                 std::string("DEX ") + purpose + " must be ASCII");
        }
        result.push_back(static_cast<char>(unit));
    }
    return result;
}

void ValidateTypeDescriptor(const std::string& descriptor,
                            const std::size_t offset) {
    if (descriptor.empty()) {
        Fail(DexErrorReason::invalid_descriptor, offset,
             "DEX type descriptor is empty");
    }
    std::size_t cursor{};
    while (cursor < descriptor.size() && descriptor[cursor] == '[') {
        ++cursor;
        if (cursor > 255) {
            Fail(DexErrorReason::invalid_descriptor, offset,
                 "DEX array descriptor exceeds 255 dimensions");
        }
    }
    if (cursor == descriptor.size()) {
        Fail(DexErrorReason::invalid_descriptor, offset,
             "DEX array descriptor has no component type");
    }
    const auto kind = descriptor[cursor];
    if (kind == 'L') {
        if (descriptor.back() != ';' || cursor + 2 >= descriptor.size()) {
            Fail(DexErrorReason::invalid_descriptor, offset,
                 "DEX object descriptor is malformed");
        }
        for (std::size_t index = cursor + 1; index + 1 < descriptor.size();
             ++index) {
            const auto character = descriptor[index];
            if (character == '.' || character == '[' || character == ';' ||
                character == '\0') {
                Fail(DexErrorReason::invalid_descriptor, offset,
                     "DEX object descriptor contains an invalid character");
            }
        }
        return;
    }
    constexpr std::string_view primitives = "VZBSCIJFD";
    if (cursor + 1 != descriptor.size() ||
        primitives.find(kind) == std::string_view::npos ||
        (cursor != 0 && kind == 'V')) {
        Fail(DexErrorReason::invalid_descriptor, offset,
             "DEX primitive descriptor is malformed");
    }
}

[[nodiscard]] char ShortyKind(const std::string& descriptor) {
    return descriptor.front() == '[' || descriptor.front() == 'L'
               ? 'L'
               : descriptor.front();
}

void ReadStringsTypesAndPrototypes(const Reader& reader, DexImage& image) {
    image.strings.reserve(image.header.string_ids_size);
    for (std::uint32_t index = 0; index < image.header.string_ids_size;
         ++index) {
        const auto at = static_cast<std::size_t>(
                            image.header.string_ids_offset) +
                        static_cast<std::size_t>(index) * 4U;
        image.strings.push_back(ReadString(
            reader, image.header, reader.U32(at)));
    }

    image.types.reserve(image.header.type_ids_size);
    for (std::uint32_t index = 0; index < image.header.type_ids_size; ++index) {
        const auto at = static_cast<std::size_t>(image.header.type_ids_offset) +
                        static_cast<std::size_t>(index) * 4U;
        const auto string_index = reader.U32(at);
        if (string_index >= image.strings.size()) {
            Fail(DexErrorReason::invalid_index, at,
                 "DEX type descriptor string index is invalid");
        }
        auto descriptor = RequireAscii(image.strings[string_index],
                                       "type descriptor");
        ValidateTypeDescriptor(descriptor, at);
        image.types.push_back({string_index, std::move(descriptor)});
    }

    image.prototypes.reserve(image.header.proto_ids_size);
    for (std::uint32_t index = 0; index < image.header.proto_ids_size;
         ++index) {
        const auto at = static_cast<std::size_t>(image.header.proto_ids_offset) +
                        static_cast<std::size_t>(index) * 12U;
        const auto shorty_index = reader.U32(at);
        const auto return_index = reader.U32(at + 4);
        const auto parameters_offset = reader.U32(at + 8);
        if (shorty_index >= image.strings.size() ||
            return_index >= image.types.size()) {
            Fail(DexErrorReason::invalid_index, at,
                 "DEX prototype string or return type index is invalid");
        }
        DexPrototype prototype{shorty_index, return_index, {}};
        if (parameters_offset != 0) {
            if ((parameters_offset & 3U) != 0 ||
                parameters_offset < image.header.data_offset) {
                Fail(DexErrorReason::invalid_range, parameters_offset,
                     "DEX prototype type_list offset is invalid");
            }
            const auto count = reader.U32(parameters_offset);
            reader.Require(static_cast<std::size_t>(parameters_offset) + 4U,
                           static_cast<std::size_t>(count) * 2U);
            prototype.parameter_type_indices.reserve(count);
            for (std::uint32_t parameter = 0; parameter < count; ++parameter) {
                const auto type_index = reader.U16(
                    static_cast<std::size_t>(parameters_offset) + 4U +
                    static_cast<std::size_t>(parameter) * 2U);
                if (type_index >= image.types.size()) {
                    Fail(DexErrorReason::invalid_index, parameters_offset,
                         "DEX prototype parameter type index is invalid");
                }
                prototype.parameter_type_indices.push_back(type_index);
            }
        }
        const auto shorty = RequireAscii(image.strings[shorty_index],
                                         "prototype shorty");
        if (shorty.size() != prototype.parameter_type_indices.size() + 1U ||
            shorty.front() != ShortyKind(image.types[return_index].descriptor)) {
            Fail(DexErrorReason::invalid_prototype, at,
                 "DEX prototype shorty does not match result or arity");
        }
        for (std::size_t parameter = 0;
             parameter < prototype.parameter_type_indices.size(); ++parameter) {
            const auto& descriptor = image.types[
                prototype.parameter_type_indices[parameter]].descriptor;
            if (descriptor == "V" || shorty[parameter + 1] !=
                                         ShortyKind(descriptor)) {
                Fail(DexErrorReason::invalid_prototype, at,
                     "DEX prototype shorty does not match parameter types");
            }
        }
        image.prototypes.push_back(std::move(prototype));
    }
}

[[nodiscard]] bool IsClassType(const DexImage& image,
                               const std::uint32_t index) {
    return index < image.types.size() &&
           image.types[index].descriptor.front() == 'L';
}

// dex-format 允许 method 引用的 owner 为数组类型(数组继承 Object.clone();
// AOSP libdex/DexFile.h 的 method_id 亦不限制 owner 为 class descriptor)。
// field 引用的 owner 仍必须是 class descriptor。
[[nodiscard]] bool IsMethodOwnerType(const DexImage& image,
                                     const std::uint32_t index) {
    if (index >= image.types.size()) return false;
    const auto front = image.types[index].descriptor.front();
    return front == 'L' || front == '[';
}

void RequireDataOffset(const DexHeader& header, const std::uint32_t offset,
                       const char* name) {
    if (offset != 0 &&
        (offset < header.data_offset || offset >= header.file_size)) {
        Fail(DexErrorReason::invalid_range, offset,
             std::string("DEX ") + name + " is outside data section");
    }
}

[[nodiscard]] std::vector<std::uint32_t> ReadTypeList(
    const Reader& reader, const DexImage& image, const std::uint32_t offset) {
    if (offset == 0) return {};
    if ((offset & 3U) != 0 || offset < image.header.data_offset) {
        Fail(DexErrorReason::invalid_range, offset,
             "DEX class interface type_list offset is invalid");
    }
    const auto count = reader.U32(offset);
    reader.Require(static_cast<std::size_t>(offset) + 4U,
                   static_cast<std::size_t>(count) * 2U);
    std::vector<std::uint32_t> result;
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto type = reader.U16(static_cast<std::size_t>(offset) + 4U +
                                     static_cast<std::size_t>(index) * 2U);
        if (!IsClassType(image, type) ||
            std::find(result.begin(), result.end(), type) != result.end()) {
            Fail(DexErrorReason::invalid_class_def, offset,
                 "DEX class interface is invalid or duplicated");
        }
        result.push_back(type);
    }
    return result;
}

void ReadMembersAndClasses(const Reader& reader, DexImage& image) {
    image.fields.reserve(image.header.field_ids_size);
    for (std::uint32_t index = 0; index < image.header.field_ids_size;
         ++index) {
        const auto at = static_cast<std::size_t>(image.header.field_ids_offset) +
                        static_cast<std::size_t>(index) * 8U;
        const auto declaring_type = reader.U16(at);
        const auto field_type = reader.U16(at + 2);
        const auto name = reader.U32(at + 4);
        if (!IsClassType(image, declaring_type) ||
            field_type >= image.types.size() || name >= image.strings.size()) {
            Fail(DexErrorReason::invalid_member, at,
                 "DEX field_id contains an invalid index");
        }
        image.fields.push_back({declaring_type, field_type, name});
    }

    image.methods.reserve(image.header.method_ids_size);
    for (std::uint32_t index = 0; index < image.header.method_ids_size;
         ++index) {
        const auto at =
            static_cast<std::size_t>(image.header.method_ids_offset) +
            static_cast<std::size_t>(index) * 8U;
        const auto declaring_type = reader.U16(at);
        const auto prototype = reader.U16(at + 2);
        const auto name = reader.U32(at + 4);
        if (!IsMethodOwnerType(image, declaring_type) ||
            prototype >= image.prototypes.size() ||
            name >= image.strings.size()) {
            Fail(DexErrorReason::invalid_member, at,
                 "DEX method_id contains an invalid index");
        }
        image.methods.push_back({declaring_type, prototype, name});
    }

    image.classes.reserve(image.header.class_defs_size);
    std::vector<std::uint32_t> declared_types;
    for (std::uint32_t index = 0; index < image.header.class_defs_size;
         ++index) {
        const auto at =
            static_cast<std::size_t>(image.header.class_defs_offset) +
            static_cast<std::size_t>(index) * 32U;
        const auto class_type = reader.U32(at);
        const auto access_flags = reader.U32(at + 4);
        const auto superclass = reader.U32(at + 8);
        const auto interfaces_offset = reader.U32(at + 12);
        const auto source_file = reader.U32(at + 16);
        const auto annotations_offset = reader.U32(at + 20);
        const auto class_data_offset = reader.U32(at + 24);
        const auto static_values_offset = reader.U32(at + 28);
        if (!IsClassType(image, class_type) ||
            std::find(declared_types.begin(), declared_types.end(),
                      class_type) != declared_types.end()) {
            Fail(DexErrorReason::invalid_class_def, at,
                 "DEX class_def type is invalid or duplicated");
        }
        if (superclass != 0xffffffffU && !IsClassType(image, superclass)) {
            Fail(DexErrorReason::invalid_class_def, at + 8,
                 "DEX class_def superclass is invalid");
        }
        if (source_file != 0xffffffffU && source_file >= image.strings.size()) {
            Fail(DexErrorReason::invalid_class_def, at + 16,
                 "DEX class_def source file index is invalid");
        }
        RequireDataOffset(image.header, annotations_offset, "annotations");
        RequireDataOffset(image.header, class_data_offset, "class_data");
        RequireDataOffset(image.header, static_values_offset, "static_values");
        image.classes.push_back(
            {class_type,
             access_flags,
             superclass == 0xffffffffU
                 ? std::nullopt
                 : std::optional<std::uint32_t>{superclass},
             ReadTypeList(reader, image, interfaces_offset),
             source_file == 0xffffffffU
                 ? std::nullopt
                 : std::optional<std::uint32_t>{source_file},
             annotations_offset,
             class_data_offset,
             static_values_offset});
        declared_types.push_back(class_type);
    }
}

[[nodiscard]] std::uint64_t ReadUnsignedPayload(
    const Reader& reader, std::size_t& offset, const std::uint8_t argument) {
    std::uint64_t value{};
    for (std::uint8_t index = 0; index <= argument; ++index) {
        value |= static_cast<std::uint64_t>(reader.U8(offset++)) <<
                 (static_cast<std::uint32_t>(index) * 8U);
    }
    return value;
}

constexpr std::uint32_t kMaxEncodedValueDepth = 16U;
constexpr std::uint32_t kMaxAnnotationElements = 1024U;
constexpr std::uint32_t kMaxEncodedArraySize = 65535U;
// Work and storage scale with checked input size, independently of the guest
// heap. Fixed ceilings bound hostile alias fan-out even when items are cached.
class AnnotationBudget final {
public:
    AnnotationBudget(const DexImage& image, const DexAnnotationLimits& limits,
                     DexAnnotationStatistics& statistics)
        : header_(image.header), limits_(limits), statistics_(statistics) {
        statistics_.work_limit = std::min<std::uint64_t>(
            limits.max_work_units, 65536ULL + 8ULL * header_.data_size);
        statistics_.storage_limit = std::min<std::uint64_t>(
            limits.max_storage_bytes, 1024ULL * 1024ULL +
                                          256ULL * header_.file_size);
    }

    void Work(const std::uint64_t amount, const std::size_t offset) {
        Charge("work_units", statistics_.work_units, amount,
               statistics_.work_limit, offset);
    }
    void Storage(const std::uint64_t amount, const std::size_t offset) {
        Charge("storage_bytes", statistics_.storage_bytes, amount,
               statistics_.storage_limit, offset);
    }
    void Node(std::uint64_t& item_nodes, const std::size_t offset) {
        Charge("item_nodes", item_nodes, 1U, limits_.max_item_nodes, offset);
        Work(1U, offset);
        ++statistics_.decoded_nodes;
    }
    void RequireData(const Reader& reader, const std::size_t offset,
                     const std::uint64_t size, const bool aligned = false) const {
        if (offset < header_.data_offset || (aligned && (offset & 3U) != 0U)) {
            Fail(DexErrorReason::invalid_range, offset,
                 "DEX annotation offset is outside data or misaligned");
        }
        const auto end = static_cast<std::uint64_t>(header_.data_offset) +
                         header_.data_size;
        if (offset > end || size > end - offset) {
            Fail(DexErrorReason::truncated, offset,
                 "DEX annotation input is truncated");
        }
        reader.Require(offset, static_cast<std::size_t>(size));
    }
    template <class T>
    void Reserve(std::vector<T>& values, const std::size_t count,
                 const std::size_t offset) {
        if (count <= values.capacity()) return;
        Storage(static_cast<std::uint64_t>(count - values.capacity()) *
                    sizeof(T), offset);
        values.reserve(count);
    }
    template <class T>
    void Grow(std::vector<T>& values, const std::size_t offset) {
        if (values.size() == values.capacity()) {
            Reserve(values, values.empty() ? 1U : values.size() * 2U, offset);
        }
    }

private:
    void Charge(const char* kind, std::uint64_t& used,
                const std::uint64_t amount, const std::uint64_t limit,
                const std::size_t offset) const {
        if (amount > limit - used) {
            Fail(DexErrorReason::resource_limit, offset,
                 std::string("DEX annotation budget exceeded: kind=") + kind +
                     " used=" + std::to_string(used) +
                     " requested=" + std::to_string(amount) +
                     " limit=" + std::to_string(limit) +
                     " offset=" + std::to_string(offset) +
                     " data_size=" + std::to_string(header_.data_size));
        }
        used += amount;
    }
    const DexHeader& header_;
    const DexAnnotationLimits& limits_;
    DexAnnotationStatistics& statistics_;
};

[[nodiscard]] std::int64_t SignExtendPayload(const std::uint64_t value,
                                             const std::uint8_t argument) {
    const auto width = static_cast<std::uint32_t>(argument + 1U) * 8U;
    if (width >= 64U) {
        return static_cast<std::int64_t>(value);
    }
    auto extended = value;
    if ((extended & (1ULL << (width - 1U))) != 0U) {
        extended |= ~((1ULL << width) - 1ULL);
    }
    return static_cast<std::int64_t>(extended);
}

DexAnnotationValue ReadEncodedValue(const Reader& reader, const DexImage& image,
                                    std::size_t& offset, std::uint32_t depth,
                                    std::uint64_t& nodes, AnnotationBudget& budget);

[[nodiscard]] DexAnnotationValue ReadEncodedAnnotationBody(
    const Reader& reader, const DexImage& image, std::size_t& offset,
    const std::uint32_t depth, std::uint64_t& nodes, AnnotationBudget& budget) {
    if (depth > kMaxEncodedValueDepth) {
        Fail(DexErrorReason::invalid_member, offset,
             "DEX encoded annotation nesting is too deep");
    }
    budget.Node(nodes, offset);
    DexAnnotationValue value;
    value.kind = DexAnnotationValueKind::annotation;
    const auto type_index = reader.Uleb128(offset);
    if (type_index >= image.types.size()) {
        Fail(DexErrorReason::invalid_index, offset,
             "DEX annotation type index is invalid");
    }
    value.nested_type_index = type_index;
    const auto count = reader.Uleb128(offset);
    if (count > kMaxAnnotationElements) {
        Fail(DexErrorReason::invalid_member, offset,
             "DEX encoded annotation has too many elements");
    }
    std::vector<std::uint32_t> names;
    budget.RequireData(reader, offset, 2ULL * count);
    budget.Reserve(names, count, offset);
    budget.Reserve(value.nested_elements, count, offset);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto name_index = reader.Uleb128(offset);
        if (name_index >= image.strings.size()) {
            Fail(DexErrorReason::invalid_index, offset,
                 "DEX annotation element name index is invalid");
        }
        names.push_back(name_index);
        DexAnnotationElement element;
        element.name_string_index = name_index;
        element.value =
            ReadEncodedValue(reader, image, offset, depth + 1U, nodes, budget);
        value.nested_elements.push_back(std::move(element));
    }
    // Check duplicate names without quadratic comparisons on large annotations.
    budget.Work(static_cast<std::uint64_t>(count) *
                    (static_cast<std::uint32_t>(std::bit_width(count)) + 1U), offset);
    std::sort(names.begin(), names.end());
    if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
        Fail(DexErrorReason::invalid_member, offset,
             "DEX annotation element name is duplicated");
    }
    return value;
}

DexAnnotationValue ReadEncodedValue(const Reader& reader, const DexImage& image,
                                    std::size_t& offset,
                                    const std::uint32_t depth,
                                    std::uint64_t& nodes, AnnotationBudget& budget) {
    if (depth > kMaxEncodedValueDepth) {
        Fail(DexErrorReason::invalid_member, offset,
             "DEX encoded value nesting is too deep");
    }
    budget.Node(nodes, offset);
    const auto header = reader.U8(offset++);
    const auto type = static_cast<std::uint8_t>(header & 0x1fU);
    const auto argument = static_cast<std::uint8_t>(header >> 5U);
    std::optional<std::uint8_t> maximum_argument;
    switch (type) {
    case 0x00U:  // byte
        maximum_argument = std::uint8_t{0};
        break;
    case 0x02U:  // short
    case 0x03U:  // char
        maximum_argument = std::uint8_t{1};
        break;
    case 0x04U:  // int
    case 0x10U:  // float
    case 0x15U:  // method type
    case 0x16U:  // method handle
    case 0x17U:  // string
    case 0x18U:  // type
    case 0x19U:  // field
    case 0x1aU:  // method
    case 0x1bU:  // enum
        maximum_argument = std::uint8_t{3};
        break;
    case 0x06U:  // long
    case 0x11U:  // double
        maximum_argument = std::uint8_t{7};
        break;
    default:
        break;
    }
    DexAnnotationValue value;
    if (maximum_argument.has_value()) {
        if (argument > *maximum_argument) {
            Fail(DexErrorReason::invalid_member, offset - 1U,
                 "DEX encoded value has an invalid value_arg");
        }
        const auto raw = ReadUnsignedPayload(reader, offset, argument);
        switch (type) {
        case 0x00U:
            value.kind = DexAnnotationValueKind::byte_value;
            value.integral = SignExtendPayload(raw, argument);
            break;
        case 0x02U:
            value.kind = DexAnnotationValueKind::short_value;
            value.integral = SignExtendPayload(raw, argument);
            break;
        case 0x03U:
            value.kind = DexAnnotationValueKind::char_value;
            value.integral = static_cast<std::int64_t>(raw);
            break;
        case 0x04U:
            value.kind = DexAnnotationValueKind::int_value;
            value.integral = SignExtendPayload(raw, argument);
            break;
        case 0x06U:
            value.kind = DexAnnotationValueKind::long_value;
            value.integral = SignExtendPayload(raw, argument);
            break;
        case 0x10U: {
            value.kind = DexAnnotationValueKind::float_value;
            const auto bits = static_cast<std::uint32_t>(
                raw << ((3U - argument) * 8U));
            value.floating = std::bit_cast<float>(bits);
            break;
        }
        case 0x11U: {
            value.kind = DexAnnotationValueKind::double_value;
            const auto bits = raw << ((7U - argument) * 8U);
            value.floating = std::bit_cast<double>(bits);
            break;
        }
        case 0x15U:
            value.kind = DexAnnotationValueKind::method_type_index;
            value.index = static_cast<std::uint32_t>(raw);
            break;
        case 0x16U:
            value.kind = DexAnnotationValueKind::method_handle_index;
            value.index = static_cast<std::uint32_t>(raw);
            break;
        case 0x17U:
            value.kind = DexAnnotationValueKind::string_index;
            value.index = static_cast<std::uint32_t>(raw);
            if (value.index >= image.strings.size()) {
                Fail(DexErrorReason::invalid_index, offset,
                     "DEX encoded string index is invalid");
            }
            break;
        case 0x18U:
            value.kind = DexAnnotationValueKind::type_index;
            value.index = static_cast<std::uint32_t>(raw);
            if (value.index >= image.types.size()) {
                Fail(DexErrorReason::invalid_index, offset,
                     "DEX encoded type index is invalid");
            }
            break;
        case 0x19U:
            value.kind = DexAnnotationValueKind::field_index;
            value.index = static_cast<std::uint32_t>(raw);
            if (value.index >= image.fields.size()) {
                Fail(DexErrorReason::invalid_index, offset,
                     "DEX encoded field index is invalid");
            }
            break;
        case 0x1aU:
            value.kind = DexAnnotationValueKind::method_index;
            value.index = static_cast<std::uint32_t>(raw);
            if (value.index >= image.methods.size()) {
                Fail(DexErrorReason::invalid_index, offset,
                     "DEX encoded method index is invalid");
            }
            break;
        case 0x1bU:
            value.kind = DexAnnotationValueKind::enum_field_index;
            value.index = static_cast<std::uint32_t>(raw);
            if (value.index >= image.fields.size()) {
                Fail(DexErrorReason::invalid_index, offset,
                     "DEX encoded enum field index is invalid");
            }
            break;
        default:
            break;
        }
        return value;
    }
    if (type == 0x1cU) {
        if (argument != 0U) {
            Fail(DexErrorReason::invalid_member, offset - 1U,
                 "DEX encoded array has a value_arg");
        }
        const auto count = reader.Uleb128(offset);
        if (count > kMaxEncodedArraySize) {
            Fail(DexErrorReason::invalid_member, offset,
                 "DEX encoded array is too large");
        }
        value.kind = DexAnnotationValueKind::array;
        budget.RequireData(reader, offset, count);
        budget.Reserve(value.values, count, offset);
        for (std::uint32_t index = 0; index < count; ++index) {
            value.values.push_back(
                ReadEncodedValue(reader, image, offset, depth + 1U, nodes, budget));
        }
        return value;
    }
    if (type == 0x1dU) {
        if (argument != 0U) {
            Fail(DexErrorReason::invalid_member, offset - 1U,
                 "DEX encoded annotation has a value_arg");
        }
        return ReadEncodedAnnotationBody(reader, image, offset, depth + 1U,
                                         nodes, budget);
    }
    if (type == 0x1eU) {
        if (argument != 0U) {
            Fail(DexErrorReason::invalid_member, offset - 1U,
                 "DEX encoded null has a value_arg");
        }
        value.kind = DexAnnotationValueKind::null_reference;
        return value;
    }
    if (type == 0x1fU) {
        if (argument > 1U) {
            Fail(DexErrorReason::invalid_member, offset - 1U,
                 "DEX encoded boolean has an invalid value_arg");
        }
        value.kind = DexAnnotationValueKind::boolean_value;
        value.integral = argument;
        return value;
    }
    Fail(DexErrorReason::invalid_member, offset - 1U,
         "DEX encoded annotation value type is invalid");
}

[[nodiscard]] DexRuntimeAnnotation ReadAnnotationItem(
    const Reader& reader, const DexImage& image,
    const std::uint32_t annotation_offset, std::uint64_t& nodes, AnnotationBudget& budget) {
    std::size_t offset = annotation_offset;
    const auto visibility = reader.U8(offset++);
    if (visibility > 2U) {
        Fail(DexErrorReason::invalid_member, annotation_offset,
             "DEX annotation visibility is invalid");
    }
    auto body = ReadEncodedAnnotationBody(reader, image, offset, 0U, nodes, budget);
    DexRuntimeAnnotation annotation;
    annotation.visibility = static_cast<DexAnnotationVisibility>(visibility);
    annotation.type_index = body.nested_type_index;
    annotation.elements = std::move(body.nested_elements);
    annotation.has_elements = !annotation.elements.empty();
    return annotation;
}

// Byte charge for a deep owned copy. Recursive payloads are bounded before
// this traversal, and the result is cached once per unique annotation item.
std::uint64_t AnnotationElementsBytes(
    const std::vector<DexAnnotationElement>& elements);

std::uint64_t AnnotationValueBytes(const DexAnnotationValue& value) {
    auto bytes = static_cast<std::uint64_t>(value.values.size()) *
                 sizeof(DexAnnotationValue);
    for (const auto& child : value.values) bytes += AnnotationValueBytes(child);
    return bytes + AnnotationElementsBytes(value.nested_elements);
}

std::uint64_t AnnotationElementsBytes(
    const std::vector<DexAnnotationElement>& elements) {
    auto bytes = static_cast<std::uint64_t>(elements.size()) *
                 sizeof(DexAnnotationElement);
    for (const auto& element : elements) bytes += AnnotationValueBytes(element.value);
    return bytes;
}

struct CachedAnnotation final {
    DexRuntimeAnnotation annotation;
    std::uint64_t nodes{};
    std::uint64_t payload_bytes{};
    std::uint32_t offset{};
};

class AnnotationReader final {
public:
    AnnotationReader(const Reader& reader, DexImage& image,
                     AnnotationBudget& budget)
        : reader_(reader), image_(image), budget_(budget) {}

    const std::vector<const CachedAnnotation*>& Set(const std::uint32_t offset) {
        budget_.Work(1U, offset);
        if (offset == 0U) return empty_;
        const auto found = sets_.find(offset);
        if (found != sets_.end()) {
            ++image_.annotation_statistics.set_cache_hits;
            return found->second;
        }
        budget_.RequireData(reader_, offset, 4U, true);
        const auto count = reader_.U32(offset);
        budget_.RequireData(reader_, offset, 4ULL + 4ULL * count, true);
        budget_.Storage(sizeof(decltype(sets_)::value_type) + 4U * sizeof(void*), offset);
        std::vector<const CachedAnnotation*> annotations;
        budget_.Reserve(annotations, count, offset);
        for (std::uint32_t index = 0; index < count; ++index) {
            annotations.push_back(&Item(reader_.U32(
                static_cast<std::size_t>(offset) + 4U + 4ULL * index)));
        }
        ++image_.annotation_statistics.unique_sets;
        return sets_.emplace(offset, std::move(annotations)).first->second;
    }

    void CopyPayload(const CachedAnnotation& item) {
        budget_.Work(item.nodes, item.offset);
        budget_.Storage(item.payload_bytes, item.offset);
    }

private:
    const CachedAnnotation& Item(const std::uint32_t offset) {
        budget_.Work(1U, offset);
        const auto found = items_.find(offset);
        if (found != items_.end()) {
            ++image_.annotation_statistics.item_cache_hits;
            return found->second;
        }
        budget_.RequireData(reader_, offset, 3U);
        budget_.Storage(sizeof(decltype(items_)::value_type) + 4U * sizeof(void*), offset);
        CachedAnnotation item;
        item.offset = offset;
        item.annotation = ReadAnnotationItem(reader_, image_, offset, item.nodes, budget_);
        budget_.Work(item.nodes, offset);  // payload-size traversal
        item.payload_bytes = AnnotationElementsBytes(item.annotation.elements);
        ++image_.annotation_statistics.unique_items;
        return items_.emplace(offset, std::move(item)).first->second;
    }
    const Reader& reader_;
    DexImage& image_;
    AnnotationBudget& budget_;
    std::map<std::uint32_t, CachedAnnotation> items_;
    std::map<std::uint32_t, std::vector<const CachedAnnotation*>> sets_;
    const std::vector<const CachedAnnotation*> empty_;
};

void ApplySystemClassAnnotation(const DexImage& image,
                                const DexRuntimeAnnotation& annotation,
                                DexClassSystemMetadata& metadata,
                                std::optional<DexRuntimeAnnotation>& defaults,
                                const std::uint32_t declaring_type_index,
                                AnnotationBudget& budget,
                                const CachedAnnotation& cached) {
    budget.Work(annotation.elements.size(), cached.offset);
    const auto& descriptor = image.types[annotation.type_index].descriptor;
    const auto element_name = [&](const DexAnnotationElement& element) {
        return std::u16string_view(image.strings[element.name_string_index].value);
    };
    const auto fail_type = [](const char* label) {
        Fail(DexErrorReason::invalid_member, 0,
             std::string("DEX system annotation ") + label +
                 " value has the wrong type");
    };
    if (descriptor == "Ldalvik/annotation/InnerClass;") {
        metadata.has_inner_class = true;
        for (const auto& element : annotation.elements) {
            const auto name = element_name(element);
            if (name == u"name") {
                if (element.value.kind == DexAnnotationValueKind::null_reference) {
                    metadata.inner_name = std::nullopt;
                } else if (element.value.kind ==
                           DexAnnotationValueKind::string_index) {
                    budget.Work(image.strings[element.value.index].value.size(), cached.offset);
                    budget.Storage(image.strings[element.value.index].value.size() + 1U,
                                   cached.offset);
                    metadata.inner_name = NarrowString(
                        image.strings[element.value.index].value);
                } else {
                    fail_type("name");
                }
            } else if (name == u"accessFlags") {
                if (element.value.kind != DexAnnotationValueKind::int_value) {
                    Fail(DexErrorReason::invalid_member, 0,
                         "DEX system annotation accessFlags is not int");
                }
                metadata.inner_access_flags =
                    static_cast<std::uint32_t>(element.value.integral);
            }
        }
    } else if (descriptor == "Ldalvik/annotation/EnclosingClass;") {
        for (const auto& element : annotation.elements) {
            if (element_name(element) != u"value") continue;
            if (element.value.kind != DexAnnotationValueKind::type_index) {
                fail_type("enclosing class");
            }
            metadata.enclosing_class_type_index = element.value.index;
        }
    } else if (descriptor == "Ldalvik/annotation/EnclosingMethod;") {
        for (const auto& element : annotation.elements) {
            if (element_name(element) != u"value") continue;
            if (element.value.kind != DexAnnotationValueKind::method_index) {
                fail_type("enclosing method");
            }
            metadata.enclosing_method_index = element.value.index;
        }
    } else if (descriptor == "Ldalvik/annotation/MemberClasses;") {
        for (const auto& element : annotation.elements) {
            if (element_name(element) != u"value") continue;
            if (element.value.kind != DexAnnotationValueKind::array) {
                Fail(DexErrorReason::invalid_member, 0,
                     "DEX system annotation value is not an array");
            }
            budget.Work(element.value.values.size(), cached.offset);
            budget.Reserve(metadata.member_class_type_indices,
                           element.value.values.size(), cached.offset);
            metadata.member_class_type_indices.clear();
            for (const auto& item : element.value.values) {
                if (item.kind != DexAnnotationValueKind::type_index) {
                    fail_type("type");
                }
                metadata.member_class_type_indices.push_back(item.index);
            }
        }
    } else if (descriptor == "Ldalvik/annotation/AnnotationDefault;") {
        if (annotation.elements.size() != 1U ||
            element_name(annotation.elements[0]) != u"value" ||
            annotation.elements[0].value.kind !=
                DexAnnotationValueKind::annotation) {
            Fail(DexErrorReason::invalid_member, 0,
                 "DEX AnnotationDefault value is not an annotation");
        }
        const auto& nested = annotation.elements[0].value;
        if (nested.nested_type_index != declaring_type_index) {
            Fail(DexErrorReason::invalid_member, 0,
                 "DEX AnnotationDefault type does not match the declaration");
        }
        DexRuntimeAnnotation stored;
        stored.visibility = DexAnnotationVisibility::system;
        stored.type_index = nested.nested_type_index;
        budget.Work(cached.nodes, cached.offset);
        budget.Storage(AnnotationElementsBytes(nested.nested_elements), cached.offset);
        stored.elements = nested.nested_elements;
        stored.has_elements = !stored.elements.empty();
        defaults = std::move(stored);
    }
}

void ApplySystemMethodAnnotation(const DexImage& image,
                                 const DexRuntimeAnnotation& annotation,
                                 DexMethodSystemMetadata& metadata,
                                 AnnotationBudget& budget,
                                 const CachedAnnotation& cached) {
    budget.Work(annotation.elements.size(), cached.offset);
    if (image.types[annotation.type_index].descriptor !=
        "Ldalvik/annotation/Throws;") {
        return;
    }
    for (const auto& element : annotation.elements) {
        if (std::u16string_view(image.strings[element.name_string_index].value) !=
                u"value" ||
            element.value.kind != DexAnnotationValueKind::array) {
            continue;
        }
        budget.Work(element.value.values.size(), cached.offset);
        budget.Reserve(metadata.exception_type_indices,
                       element.value.values.size(), cached.offset);
        metadata.exception_type_indices.clear();
        for (const auto& item : element.value.values) {
            if (item.kind != DexAnnotationValueKind::type_index) {
                Fail(DexErrorReason::invalid_member, 0,
                     "DEX system annotation type value has the wrong type");
            }
            metadata.exception_type_indices.push_back(item.index);
        }
    }
}

void ReadSystemMetadata(const Reader& reader, DexImage& image,
                        const DexAnnotationLimits& limits) {
    AnnotationBudget budget(image, limits, image.annotation_statistics);
    AnnotationReader annotations(reader, image, budget);
    budget.Reserve(image.class_system_metadata, image.classes.size(), 0U);
    budget.Reserve(image.method_system_metadata, image.methods.size(), 0U);
    budget.Reserve(image.field_runtime_metadata, image.fields.size(), 0U);
    budget.Reserve(image.class_annotation_metadata, image.classes.size(), 0U);
    image.class_system_metadata.resize(image.classes.size());
    image.method_system_metadata.resize(image.methods.size());
    image.field_runtime_metadata.resize(image.fields.size());
    image.class_annotation_metadata.resize(image.classes.size());
    for (std::size_t class_index = 0; class_index < image.classes.size();
         ++class_index) {
        const auto directory = image.classes[class_index].annotations_offset;
        if (directory == 0U) continue;
        budget.Work(1U, directory);
        budget.RequireData(reader, directory, 16U, true);
        const auto class_set = reader.U32(directory);
        const auto fields_size = reader.U32(directory + 4U);
        const auto methods_size = reader.U32(directory + 8U);
        const auto parameters_size = reader.U32(directory + 12U);
        budget.RequireData(reader, directory,
            16ULL + 8ULL * (static_cast<std::uint64_t>(fields_size) +
                            methods_size + parameters_size), true);
        const auto declaring_type = image.classes[class_index].class_type_index;
        for (const auto* cached : annotations.Set(class_set)) {
            budget.Work(1U, cached->offset);
            const auto& annotation = cached->annotation;
            if (annotation.visibility == DexAnnotationVisibility::system) {
                ApplySystemClassAnnotation(
                    image, annotation, image.class_system_metadata[class_index],
                    image.class_annotation_metadata[class_index].annotation_default,
                    declaring_type, budget, *cached);
            } else if (annotation.visibility == DexAnnotationVisibility::runtime) {
                auto& output = image.class_annotation_metadata[class_index].runtime_annotations;
                annotations.CopyPayload(*cached);
                budget.Grow(output, cached->offset);
                output.push_back(annotation);
            }
        }
        const auto fields_at = static_cast<std::size_t>(directory) + 16U;
        for (std::uint32_t index = 0; index < fields_size; ++index) {
            const auto at = fields_at + static_cast<std::size_t>(index) * 8U;
            budget.Work(1U, at);
            const auto field_index = reader.U32(at);
            if (field_index >= image.fields.size() ||
                image.fields[field_index].class_type_index != declaring_type) {
                Fail(DexErrorReason::invalid_index, at,
                     "DEX annotated field index or owner is invalid");
            }
            auto& output = image.field_runtime_metadata[field_index].annotations;
            for (const auto* cached : annotations.Set(reader.U32(at + 4U))) {
                budget.Work(1U, cached->offset);
                if (cached->annotation.visibility == DexAnnotationVisibility::runtime) {
                    annotations.CopyPayload(*cached);
                    budget.Grow(output, cached->offset);
                    output.push_back(cached->annotation);
                }
            }
        }
        const auto methods_at = fields_at + static_cast<std::size_t>(fields_size) * 8U;
        for (std::uint32_t index = 0; index < methods_size; ++index) {
            const auto at = methods_at + static_cast<std::size_t>(index) * 8U;
            budget.Work(1U, at);
            const auto method_index = reader.U32(at);
            if (method_index >= image.methods.size() ||
                image.methods[method_index].class_type_index != declaring_type) {
                Fail(DexErrorReason::invalid_index, at,
                     "DEX annotated method index or owner is invalid");
            }
            for (const auto* cached : annotations.Set(reader.U32(at + 4U))) {
                budget.Work(1U, cached->offset);
                if (cached->annotation.visibility == DexAnnotationVisibility::system) {
                    ApplySystemMethodAnnotation(image, cached->annotation,
                        image.method_system_metadata[method_index], budget, *cached);
                }
            }
        }
        const auto parameters_at = methods_at + static_cast<std::size_t>(methods_size) * 8U;
        for (std::uint32_t index = 0; index < parameters_size; ++index) {
            const auto at = parameters_at + static_cast<std::size_t>(index) * 8U;
            budget.Work(1U, at);
            const auto method_index = reader.U32(at);
            const auto list_offset = reader.U32(at + 4U);
            if (method_index >= image.methods.size() ||
                image.methods[method_index].class_type_index != declaring_type) {
                Fail(DexErrorReason::invalid_index, at,
                     "DEX annotated parameter method index or owner is invalid");
            }
            if (list_offset == 0U) continue;
            budget.RequireData(reader, list_offset, 4U, true);
            const auto count = reader.U32(list_offset);
            budget.RequireData(reader, list_offset, 4ULL + 4ULL * count, true);
            for (std::uint32_t parameter = 0; parameter < count; ++parameter) {
                budget.Work(1U, list_offset);
                for (const auto* cached : annotations.Set(reader.U32(
                         static_cast<std::size_t>(list_offset) + 4U +
                         static_cast<std::size_t>(parameter) * 4U))) {
                    budget.Work(1U, cached->offset);
                }
            }
        }
    }
}

}  // namespace

DexError::DexError(const DexErrorReason reason, const std::size_t offset,
                   std::string message)
    : std::runtime_error(std::move(message)), reason_(reason), offset_(offset) {}
DexErrorReason DexError::Reason() const noexcept { return reason_; }
std::size_t DexError::Offset() const noexcept { return offset_; }

std::optional<DexMapItem> DexImage::FindMapItem(
    const DexMapItemType type) const noexcept {
    const auto value = static_cast<std::uint16_t>(type);
    const auto found = std::find_if(
        map_items.begin(), map_items.end(),
        [value](const DexMapItem& item) { return item.type == value; });
    return found == map_items.end() ? std::nullopt
                                    : std::optional<DexMapItem>{*found};
}

DexImage ParseDex(const std::span<const std::uint8_t> bytes,
                  const DexAnnotationLimits& annotation_limits) {
    const Reader reader(bytes);
    DexImage image;
    image.header = ReadHeader(reader, bytes);
    ValidateHeader(image.header, bytes.size());
    image.map_items = ReadMap(reader, image.header, bytes.size());
    ValidateMap(image);
    ReadStringsTypesAndPrototypes(reader, image);
    ReadMembersAndClasses(reader, image);
    ReadSystemMetadata(reader, image, annotation_limits);
    return image;
}

}  // namespace ogplay::loader
