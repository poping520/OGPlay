#include "ogplay/loader/dex_code.h"

#include <bit>
#include <cstring>
#include <set>

#include "ogplay/core/byte_order.h"

#include "dex_uleb128.h"

namespace ogplay::loader {
namespace {

[[noreturn]] void Fail(const DexErrorReason reason, const std::size_t offset,
                       const char* message) {
    throw DexError(reason, offset, message);
}

class Reader final {
public:
    explicit Reader(const std::span<const std::uint8_t> bytes)
        : bytes_(bytes) {}

    [[nodiscard]] std::uint8_t U8(std::size_t& offset) const {
        Require(offset, 1);
        return bytes_[offset++];
    }
    [[nodiscard]] std::uint16_t U16(const std::size_t offset) const {
        Require(offset, 2);
        return core::ReadLittleEndian<std::uint16_t>(bytes_, offset);
    }
    [[nodiscard]] std::uint32_t U32(const std::size_t offset) const {
        Require(offset, 4);
        return core::ReadLittleEndian<std::uint32_t>(bytes_, offset);
    }
    [[nodiscard]] std::uint32_t Uleb128(std::size_t& offset) const {
        return detail::ReadUleb128(bytes_, offset,
                                   {"DEX code section is truncated",
                                    "DEX code ULEB128 exceeds 32 bits",
                                    "DEX code ULEB128 is not minimal",
                                    "DEX code ULEB128 is unterminated"});
    }
    [[nodiscard]] std::int32_t Sleb128(std::size_t& offset) const {
        std::int32_t value{};
        std::uint32_t shift{};
        for (std::uint32_t index = 0; index < 5; ++index) {
            const auto byte = U8(offset);
            value |= static_cast<std::int32_t>(
                static_cast<std::uint32_t>(byte & 0x7fU) << shift);
            shift += 7;
            if ((byte & 0x80U) == 0) {
                if (shift < 32 && (byte & 0x40U) != 0) {
                    value |= static_cast<std::int32_t>(~0U << shift);
                }
                return value;
            }
        }
        Fail(DexErrorReason::invalid_uleb128, offset,
             "DEX code SLEB128 is unterminated");
    }
    void Require(const std::size_t offset, const std::size_t size) const {
        if (!core::RangeFits(bytes_, offset, size)) {
            Fail(DexErrorReason::truncated, offset,
                 "DEX code section is truncated");
        }
    }

private:
    std::span<const std::uint8_t> bytes_;
};

[[nodiscard]] std::vector<DexTryBlock> ReadTries(
    const Reader& reader, const DexImage& image, const DexCodeInfo& code) {
    const std::size_t insns_end =
        static_cast<std::size_t>(code.offset) + 16U +
        static_cast<std::size_t>(code.instruction_units) * 2U;
    const std::size_t tries_offset =
        insns_end + ((code.instruction_units & 1U) != 0 ? 2U : 0U);
    const std::size_t handlers_offset =
        tries_offset + static_cast<std::size_t>(code.tries_size) * 8U;

    // encoded_catch_handler_list: read the declared list first so that
    // handler_off references can be validated against real entry offsets.
    std::size_t cursor = handlers_offset;
    const auto handler_count = reader.Uleb128(cursor);
    struct HandlerEntry final {
        std::vector<DexTryHandler> typed;
        std::optional<std::uint32_t> catch_all;
    };
    std::vector<std::pair<std::uint32_t, HandlerEntry>> entries;
    entries.reserve(handler_count);
    for (std::uint32_t index = 0; index < handler_count; ++index) {
        const auto entry_offset =
            static_cast<std::uint32_t>(cursor - handlers_offset);
        HandlerEntry entry;
        const auto size = reader.Sleb128(cursor);
        const auto typed_count =
            static_cast<std::uint32_t>(size < 0 ? -size : size);
        for (std::uint32_t pair = 0; pair < typed_count; ++pair) {
            const auto type_index = reader.Uleb128(cursor);
            const auto handler_pc = reader.Uleb128(cursor);
            if (type_index >= image.types.size()) {
                Fail(DexErrorReason::invalid_index, cursor,
                     "DEX catch handler type index is out of range");
            }
            if (handler_pc >= code.instruction_units) {
                Fail(DexErrorReason::invalid_range, cursor,
                     "DEX catch handler address is outside the method");
            }
            entry.typed.push_back({type_index, handler_pc});
        }
        if (size <= 0) {
            const auto catch_all_pc = reader.Uleb128(cursor);
            if (catch_all_pc >= code.instruction_units) {
                Fail(DexErrorReason::invalid_range, cursor,
                     "DEX catch-all address is outside the method");
            }
            entry.catch_all = catch_all_pc;
        }
        entries.emplace_back(entry_offset, std::move(entry));
    }

    std::vector<DexTryBlock> tries;
    tries.reserve(code.tries_size);
    std::uint32_t previous_end{};
    for (std::uint32_t index = 0; index < code.tries_size; ++index) {
        const std::size_t base = tries_offset + index * 8U;
        DexTryBlock block;
        block.start_pc = reader.U32(base);
        block.instruction_count = reader.U16(base + 4U);
        const auto handler_off = reader.U16(base + 6U);
        if (block.instruction_count == 0 ||
            block.start_pc >= code.instruction_units ||
            block.instruction_count >
                code.instruction_units - block.start_pc) {
            Fail(DexErrorReason::invalid_range, base,
                 "DEX try block range is outside the method");
        }
        if (index != 0 && block.start_pc < previous_end) {
            Fail(DexErrorReason::invalid_range, base,
                 "DEX try blocks overlap or are unsorted");
        }
        previous_end = block.start_pc + block.instruction_count;
        const HandlerEntry* found = nullptr;
        for (const auto& [entry_offset, entry] : entries) {
            if (entry_offset == handler_off) {
                found = &entry;
                break;
            }
        }
        if (found == nullptr) {
            Fail(DexErrorReason::invalid_range, base,
                 "DEX try handler offset does not name a handler entry");
        }
        block.typed_handlers = found->typed;
        block.catch_all_pc = found->catch_all;
        tries.push_back(std::move(block));
    }
    return tries;
}

[[nodiscard]] std::int64_t SignExtend(const std::uint64_t raw,
                                      const std::uint32_t size) {
    const auto bits = size * 8U;
    if (bits >= 64) return static_cast<std::int64_t>(raw);
    const auto shift = 64U - bits;
    return static_cast<std::int64_t>(raw << shift) >>
           static_cast<std::int64_t>(shift);
}

[[nodiscard]] DexEncodedValue ReadEncodedValue(const Reader& reader,
                                               const DexImage& image,
                                               std::size_t& cursor) {
    const auto lead = reader.U8(cursor);
    const auto value_type = static_cast<std::uint32_t>(lead & 0x1fU);
    const auto value_arg = static_cast<std::uint32_t>(lead >> 5U);
    const auto size = value_arg + 1U;

    const auto read_raw = [&](const std::uint32_t byte_count) {
        std::uint64_t raw{};
        for (std::uint32_t index = 0; index < byte_count; ++index) {
            raw |= static_cast<std::uint64_t>(reader.U8(cursor))
                   << (index * 8U);
        }
        return raw;
    };
    const auto require_size = [&](const std::uint32_t maximum) {
        if (size > maximum) {
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded value size exceeds its type");
        }
    };

    DexEncodedValue value;
    switch (value_type) {
        case 0x00:  // BYTE
            require_size(1);
            value.kind = DexEncodedValueKind::byte_value;
            value.integral = SignExtend(read_raw(size), size);
            break;
        case 0x02:  // SHORT
            require_size(2);
            value.kind = DexEncodedValueKind::short_value;
            value.integral = SignExtend(read_raw(size), size);
            break;
        case 0x03:  // CHAR
            require_size(2);
            value.kind = DexEncodedValueKind::char_value;
            value.integral = static_cast<std::int64_t>(read_raw(size));
            break;
        case 0x04:  // INT
            require_size(4);
            value.kind = DexEncodedValueKind::int_value;
            value.integral = SignExtend(read_raw(size), size);
            break;
        case 0x06:  // LONG
            require_size(8);
            value.kind = DexEncodedValueKind::long_value;
            value.integral = SignExtend(read_raw(size), size);
            break;
        case 0x10: {  // FLOAT: zero-extended to the right
            require_size(4);
            const auto raw = read_raw(size) << ((4U - size) * 8U);
            value.kind = DexEncodedValueKind::float_value;
            value.floating = std::bit_cast<float>(
                static_cast<std::uint32_t>(raw));
            break;
        }
        case 0x11: {  // DOUBLE: zero-extended to the right
            require_size(8);
            const auto raw = read_raw(size) << ((8U - size) * 8U);
            value.kind = DexEncodedValueKind::double_value;
            value.floating = std::bit_cast<double>(raw);
            break;
        }
        case 0x17: {  // STRING
            require_size(4);
            const auto index = static_cast<std::uint32_t>(read_raw(size));
            if (index >= image.strings.size()) {
                Fail(DexErrorReason::invalid_index, cursor,
                     "DEX encoded string index is out of range");
            }
            value.kind = DexEncodedValueKind::string_index;
            value.index = index;
            break;
        }
        case 0x18: {  // TYPE
            require_size(4);
            const auto index = static_cast<std::uint32_t>(read_raw(size));
            if (index >= image.types.size()) {
                Fail(DexErrorReason::invalid_index, cursor,
                     "DEX encoded type index is out of range");
            }
            value.kind = DexEncodedValueKind::type_index;
            value.index = index;
            break;
        }
        case 0x1e:  // NULL
            if (value_arg != 0) {
                Fail(DexErrorReason::invalid_member, cursor,
                     "DEX encoded null must have zero size");
            }
            value.kind = DexEncodedValueKind::null_reference;
            break;
        case 0x1f:  // BOOLEAN (value in value_arg)
            if (value_arg > 1) {
                Fail(DexErrorReason::invalid_member, cursor,
                     "DEX encoded boolean argument is invalid");
            }
            value.kind = DexEncodedValueKind::boolean_value;
            value.integral = value_arg;
            break;
        default:
            Fail(DexErrorReason::invalid_member, cursor,
                 "DEX encoded value type is not supported for static values");
    }
    return value;
}

}  // namespace

DexMethodCode ReadDexMethodCode(const std::span<const std::uint8_t> bytes,
                                const DexImage& image,
                                const DexCodeInfo& code) {
    if (bytes.size() != image.header.file_size) {
        Fail(DexErrorReason::invalid_header, 32,
             "DEX code bytes do not match parsed image size");
    }
    const Reader reader(bytes);
    DexMethodCode result;
    result.info = code;
    result.instructions.reserve(code.instruction_units);
    const std::size_t insns_base = static_cast<std::size_t>(code.offset) + 16U;
    reader.Require(insns_base,
                   static_cast<std::size_t>(code.instruction_units) * 2U);
    for (std::uint32_t unit = 0; unit < code.instruction_units; ++unit) {
        result.instructions.push_back(reader.U16(insns_base + unit * 2U));
    }
    if (code.tries_size != 0) {
        if (code.instruction_units == 0) {
            Fail(DexErrorReason::invalid_range, code.offset,
                 "DEX try table exists without instructions");
        }
        result.tries = ReadTries(reader, image, code);
    }
    return result;
}

std::vector<DexEncodedValue> ReadDexStaticValues(
    const std::span<const std::uint8_t> bytes, const DexImage& image,
    const std::uint32_t static_values_offset) {
    if (bytes.size() != image.header.file_size) {
        Fail(DexErrorReason::invalid_header, 32,
             "DEX static values bytes do not match parsed image size");
    }
    if (static_values_offset == 0) return {};
    if (static_values_offset < image.header.data_offset ||
        static_values_offset >= image.header.file_size) {
        Fail(DexErrorReason::invalid_range, static_values_offset,
             "DEX static values offset is outside the data section");
    }
    const Reader reader(bytes);
    std::size_t cursor = static_values_offset;
    const auto count = reader.Uleb128(cursor);
    if (count > 0xffffU) {
        Fail(DexErrorReason::invalid_member, cursor,
             "DEX static value count is implausible");
    }
    std::vector<DexEncodedValue> values;
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back(ReadEncodedValue(reader, image, cursor));
    }
    return values;
}

}  // namespace ogplay::loader
