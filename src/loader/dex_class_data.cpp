#include "ogplay/loader/dex_class_data.h"

#include <set>
#include <string>
#include <utility>

namespace ogplay::loader {
namespace {

constexpr std::uint32_t kAccPrivate = 0x0002;
constexpr std::uint32_t kAccStatic = 0x0008;
constexpr std::uint32_t kAccNative = 0x0100;
constexpr std::uint32_t kAccAbstract = 0x0400;
constexpr std::uint32_t kAccConstructor = 0x10000;

[[noreturn]] void Fail(const DexErrorReason reason, const std::size_t offset,
                       const char* message) {
    throw DexError(reason, offset, message);
}

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t U8(const std::size_t offset) const {
        Require(offset, 1);
        return bytes_[offset];
    }
    [[nodiscard]] std::uint16_t U16(const std::size_t offset) const {
        Require(offset, 2);
        const auto value = static_cast<std::uint32_t>(bytes_[offset]) |
                           static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U;
        return static_cast<std::uint16_t>(value);
    }
    [[nodiscard]] std::uint32_t U32(const std::size_t offset) const {
        Require(offset, 4);
        return static_cast<std::uint32_t>(bytes_[offset]) |
               static_cast<std::uint32_t>(bytes_[offset + 1]) << 8U |
               static_cast<std::uint32_t>(bytes_[offset + 2]) << 16U |
               static_cast<std::uint32_t>(bytes_[offset + 3]) << 24U;
    }
    [[nodiscard]] std::uint32_t Uleb128(std::size_t& offset) const {
        std::uint32_t value{};
        for (std::uint32_t index = 0; index < 5; ++index) {
            const auto byte = U8(offset++);
            if (index == 4 && (byte & 0xf0U) != 0) {
                Fail(DexErrorReason::invalid_uleb128, offset - 1,
                     "DEX class_data ULEB128 exceeds 32 bits");
            }
            value |= static_cast<std::uint32_t>(byte & 0x7fU)
                     << (index * 7U);
            if ((byte & 0x80U) == 0) {
                if (index != 0 && byte == 0) {
                    Fail(DexErrorReason::invalid_uleb128, offset - 1,
                         "DEX class_data ULEB128 is not minimal");
                }
                return value;
            }
        }
        Fail(DexErrorReason::invalid_uleb128, offset,
             "DEX class_data ULEB128 is unterminated");
    }
    void Require(const std::size_t offset, const std::size_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - offset) {
            Fail(DexErrorReason::truncated, offset,
                 "DEX class_data or code_item is truncated");
        }
    }

private:
    std::span<const std::uint8_t> bytes_;
};

[[nodiscard]] DexCodeInfo ReadCode(const Reader& reader,
                                   const DexHeader& header,
                                   const std::uint32_t offset) {
    if ((offset & 3U) != 0 || offset < header.data_offset) {
        Fail(DexErrorReason::invalid_range, offset,
             "DEX code_item offset is invalid");
    }
    const auto registers = reader.U16(offset);
    const auto incoming = reader.U16(static_cast<std::size_t>(offset) + 2U);
    const auto outgoing = reader.U16(static_cast<std::size_t>(offset) + 4U);
    const auto tries = reader.U16(static_cast<std::size_t>(offset) + 6U);
    const auto debug = reader.U32(static_cast<std::size_t>(offset) + 8U);
    const auto units = reader.U32(static_cast<std::size_t>(offset) + 12U);
    if (incoming > registers ||
        (debug != 0 &&
         (debug < header.data_offset || debug >= header.file_size))) {
        Fail(DexErrorReason::invalid_member, offset,
             "DEX code_item register or debug metadata is invalid");
    }
    const auto instruction_bytes = static_cast<std::uint64_t>(units) * 2U;
    const auto padding = tries != 0 && (units & 1U) != 0 ? 2ULL : 0ULL;
    const auto try_bytes = static_cast<std::uint64_t>(tries) * 8U;
    const auto total = 16ULL + instruction_bytes + padding + try_bytes;
    if (total > header.file_size - offset) {
        Fail(DexErrorReason::invalid_range, offset,
             "DEX code_item instructions or try table exceed file");
    }
    return {offset, registers, incoming, outgoing, tries, debug, units};
}

[[nodiscard]] std::vector<DexEncodedField> ReadFields(
    const Reader& reader, const DexImage& image, std::size_t& cursor,
    const std::uint32_t count, const std::uint32_t declaring_class,
    const bool expect_static, std::set<std::uint32_t>& seen) {
    std::vector<DexEncodedField> fields;
    fields.reserve(count);
    std::uint32_t field_index{};
    for (std::uint32_t item = 0; item < count; ++item) {
        const auto difference = reader.Uleb128(cursor);
        if (item != 0 && difference == 0) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded fields are not strictly ordered");
        }
        if (difference > UINT32_MAX - field_index) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded field index overflows");
        }
        field_index += difference;
        const auto access = reader.Uleb128(cursor);
        if (field_index >= image.fields.size() ||
            image.fields[field_index].class_type_index != declaring_class ||
            ((access & kAccStatic) != 0) != expect_static ||
            !seen.insert(field_index).second) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded field declaration is inconsistent");
        }
        fields.push_back({field_index, access});
    }
    return fields;
}

[[nodiscard]] std::vector<DexEncodedMethod> ReadMethods(
    const Reader& reader, const DexImage& image, std::size_t& cursor,
    const std::uint32_t count, const std::uint32_t declaring_class,
    const bool direct, std::set<std::uint32_t>& seen) {
    std::vector<DexEncodedMethod> methods;
    methods.reserve(count);
    std::uint32_t method_index{};
    for (std::uint32_t item = 0; item < count; ++item) {
        const auto difference = reader.Uleb128(cursor);
        if (item != 0 && difference == 0) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded methods are not strictly ordered");
        }
        if (difference > UINT32_MAX - method_index) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded method index overflows");
        }
        method_index += difference;
        const auto access = reader.Uleb128(cursor);
        const auto code_offset = reader.Uleb128(cursor);
        const bool is_direct =
            (access & (kAccPrivate | kAccStatic | kAccConstructor)) != 0;
        const bool has_no_code = (access & (kAccNative | kAccAbstract)) != 0;
        if (method_index >= image.methods.size() ||
            image.methods[method_index].class_type_index != declaring_class ||
            is_direct != direct || (code_offset == 0) != has_no_code ||
            !seen.insert(method_index).second) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded method declaration is inconsistent");
        }
        methods.push_back(
            {method_index, access,
             code_offset == 0
                 ? std::nullopt
                 : std::optional<DexCodeInfo>{
                       ReadCode(reader, image.header, code_offset)}});
    }
    return methods;
}

}  // namespace

std::vector<DexClassData> ReadDexClassData(
    const std::span<const std::uint8_t> bytes, const DexImage& image) {
    if (bytes.size() != image.header.file_size) {
        Fail(DexErrorReason::invalid_header, 32,
             "DEX class_data bytes do not match parsed image size");
    }
    const Reader reader(bytes);
    std::vector<DexClassData> result;
    result.reserve(image.classes.size());
    for (std::uint32_t class_index = 0; class_index < image.classes.size();
         ++class_index) {
        const auto& definition = image.classes[class_index];
        DexClassData data;
        data.class_def_index = class_index;
        if (definition.class_data_offset != 0) {
            std::size_t cursor = definition.class_data_offset;
            const auto static_count = reader.Uleb128(cursor);
            const auto instance_count = reader.Uleb128(cursor);
            const auto direct_count = reader.Uleb128(cursor);
            const auto virtual_count = reader.Uleb128(cursor);
            std::set<std::uint32_t> seen_fields;
            std::set<std::uint32_t> seen_methods;
            data.static_fields = ReadFields(
                reader, image, cursor, static_count,
                definition.class_type_index, true, seen_fields);
            data.instance_fields = ReadFields(
                reader, image, cursor, instance_count,
                definition.class_type_index, false, seen_fields);
            data.direct_methods = ReadMethods(
                reader, image, cursor, direct_count,
                definition.class_type_index, true, seen_methods);
            data.virtual_methods = ReadMethods(
                reader, image, cursor, virtual_count,
                definition.class_type_index, false, seen_methods);
        }
        result.push_back(std::move(data));
    }
    return result;
}

}  // namespace ogplay::loader
