#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "ogplay/loader/elf.h"

namespace ogplay::loader {

inline constexpr std::uint8_t kArmRelocationNone = 0;
inline constexpr std::uint8_t kArmRelocationAbs32 = 2;
inline constexpr std::uint8_t kArmRelocationRel32 = 3;
inline constexpr std::uint8_t kArmRelocationGlobDat = 21;
inline constexpr std::uint8_t kArmRelocationJumpSlot = 22;
inline constexpr std::uint8_t kArmRelocationRelative = 23;

class RelocationError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Elf32ResolvedSymbols final {
    std::vector<std::optional<memory::GuestAddress>> values;
};

void ApplyElf32ArmRelocations(
    const Elf32RelocationTable& relocations,
    const Elf32ResolvedSymbols& resolved_symbols,
    memory::GuestAddress load_bias, const Elf32LoadPlan& load_plan,
    memory::AddressSpace& address_space);

}  // namespace ogplay::loader
