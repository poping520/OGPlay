#pragma once

// Shared ULEB128 decoder for the DEX readers (dex, dex_class_data, dex_code).
// The decode-and-validate sequence is specified exactly once; every caller
// passes its own error texts so observable failures stay byte-identical.

#include <cstddef>
#include <cstdint>
#include <span>

#include "ogplay/loader/dex.h"

namespace ogplay::loader::detail {

struct Uleb128ErrorTexts final {
    const char* truncated;
    const char* exceeds_32_bits;
    const char* not_minimal;
    const char* unterminated;
};

[[nodiscard]] inline std::uint32_t ReadUleb128(
    const std::span<const std::uint8_t> bytes, std::size_t& offset,
    const Uleb128ErrorTexts& texts) {
    std::uint32_t value{};
    for (std::uint32_t index = 0; index < 5; ++index) {
        if (offset >= bytes.size()) {
            throw DexError(DexErrorReason::truncated, offset, texts.truncated);
        }
        const auto byte = bytes[offset++];
        if (index == 4 && (byte & 0xf0U) != 0) {
            throw DexError(DexErrorReason::invalid_uleb128, offset - 1,
                           texts.exceeds_32_bits);
        }
        value |= static_cast<std::uint32_t>(byte & 0x7fU) << (index * 7U);
        if ((byte & 0x80U) == 0) {
            if (index != 0 && byte == 0) {
                throw DexError(DexErrorReason::invalid_uleb128, offset - 1,
                               texts.not_minimal);
            }
            return value;
        }
    }
    throw DexError(DexErrorReason::invalid_uleb128, offset,
                   texts.unterminated);
}

}  // namespace ogplay::loader::detail
