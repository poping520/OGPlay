#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "ogplay/loader/dex_code.h"

namespace ogplay::runtime::dexvm {

inline constexpr std::uint32_t kInvalidFastIndex = 0xffffffffU;

enum class FastHandler : std::uint16_t {
    bridge,
    straight,
    object,
    invoke,
};

enum class FastPayloadKind : std::uint8_t {
    packed_switch,
    sparse_switch,
    array_data,
};

struct FastInstruction final {
    FastHandler handler{FastHandler::bridge};
    std::uint8_t opcode{};
    std::uint8_t width{};
    std::uint16_t a{};
    std::uint16_t b{};
    std::uint16_t c{};
    std::uint64_t extra{};
    std::uint32_t dex_pc{};
    std::uint32_t branch_target{kInvalidFastIndex};
    std::uint32_t payload{kInvalidFastIndex};
};

struct FastPayload final {
    FastPayloadKind kind{FastPayloadKind::packed_switch};
    std::uint32_t dex_pc{};
    std::uint32_t element_width{};
    std::uint32_t element_count{};
    std::uint32_t tick_weight{};
    std::vector<std::int32_t> keys;
    std::vector<std::uint32_t> targets;
    std::vector<std::uint16_t> data_units;
};

// Read-only derivative of one validated dex method. The original u2 stream is
// never modified and remains the switch interpreter's source of truth.
struct FastCode final {
    std::vector<FastInstruction> instructions;
    std::vector<FastPayload> payloads;
    std::vector<std::uint32_t> dex_pc_to_index;
    std::uint64_t storage_bytes{};

    [[nodiscard]] std::uint32_t IndexForDexPc(std::uint32_t dex_pc) const;
};

// The caller must run DexClassLinker::PrecheckMethod first. The builder still
// validates every boundary it consumes so it cannot turn malformed code into
// an executable cache.
[[nodiscard]] FastCode BuildFastCode(const loader::DexMethodCode& code,
                                     std::string_view where);

}  // namespace ogplay::runtime::dexvm
