#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ogplay/loader/dex.h"
#include "ogplay/loader/dex_class_data.h"

namespace ogplay::loader {

// L2 checked reading of method bodies and class static initial values.
// Structure layout follows AOSP libdex (DexFile.h, DexCatch.h, Leb128.h) at
// the pinned baseline; every out-of-range offset, index or malformed
// encoding fails with DexError instead of guessing.

struct DexTryHandler final {
    std::uint32_t type_index{};
    std::uint32_t handler_pc{};
};

struct DexTryBlock final {
    std::uint32_t start_pc{};
    std::uint32_t instruction_count{};
    std::vector<DexTryHandler> typed_handlers;
    std::optional<std::uint32_t> catch_all_pc;
};

struct DexMethodCode final {
    DexCodeInfo info;
    std::vector<std::uint16_t> instructions;
    std::vector<DexTryBlock> tries;
};

enum class DexEncodedValueKind : std::uint8_t {
    boolean_value,
    byte_value,
    short_value,
    char_value,
    int_value,
    long_value,
    float_value,
    double_value,
    string_index,
    type_index,
    null_reference,
};

struct DexEncodedValue final {
    DexEncodedValueKind kind{DexEncodedValueKind::null_reference};
    std::int64_t integral{};
    double floating{};
    std::uint32_t index{};
};

[[nodiscard]] DexMethodCode ReadDexMethodCode(
    std::span<const std::uint8_t> bytes, const DexImage& image,
    const DexCodeInfo& code);

[[nodiscard]] std::vector<DexEncodedValue> ReadDexStaticValues(
    std::span<const std::uint8_t> bytes, const DexImage& image,
    std::uint32_t static_values_offset);

}  // namespace ogplay::loader
