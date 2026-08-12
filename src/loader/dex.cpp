#include "ogplay/loader/dex.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string_view>
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
        std::uint32_t value{};
        for (std::uint32_t index = 0; index < 5; ++index) {
            const auto byte = U8(offset++);
            if (index == 4 && (byte & 0xf0U) != 0) {
                Fail(DexErrorReason::invalid_uleb128, offset - 1,
                     "DEX ULEB128 exceeds 32 bits");
            }
            value |= static_cast<std::uint32_t>(byte & 0x7fU)
                     << (index * 7U);
            if ((byte & 0x80U) == 0) {
                if (index != 0 && byte == 0) {
                    Fail(DexErrorReason::invalid_uleb128, offset - 1,
                         "DEX ULEB128 is not minimally encoded");
                }
                return value;
            }
        }
        Fail(DexErrorReason::invalid_uleb128, offset,
             "DEX ULEB128 is unterminated");
    }

private:
    std::span<const std::uint8_t> bytes_;
};

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

DexImage ParseDex(const std::span<const std::uint8_t> bytes) {
    const Reader reader(bytes);
    DexImage image;
    image.header = ReadHeader(reader, bytes);
    ValidateHeader(image.header, bytes.size());
    image.map_items = ReadMap(reader, image.header, bytes.size());
    ValidateMap(image);
    ReadStringsTypesAndPrototypes(reader, image);
    ReadMembersAndClasses(reader, image);
    return image;
}

}  // namespace ogplay::loader
