#include "ogplay/loader/relocation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace ogplay::loader {
namespace {

[[nodiscard]] memory::GuestAddress Biased(
    const memory::GuestAddress address, const memory::GuestAddress bias) {
    const auto value = static_cast<std::uint64_t>(address.Value()) + bias.Value();
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw RelocationError("relocation address wraps the guest address space");
    }
    return memory::GuestAddress{static_cast<std::uint32_t>(value)};
}

[[nodiscard]] std::uint32_t ReadWord(memory::AddressSpace& address_space,
                                     const memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    address_space.Read(address, bytes);
    std::uint32_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

void WriteWord(memory::AddressSpace& address_space,
               const memory::GuestAddress address, const std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    address_space.Write(address, bytes);
}

struct PendingWrite final {
    memory::GuestAddress target;
    std::uint32_t original{};
    std::uint32_t value{};
};

[[nodiscard]] std::uint32_t ResolveValue(
    const Elf32Relocation& relocation, const std::uint32_t addend,
    const memory::GuestAddress runtime_target,
    const memory::GuestAddress load_bias,
    const Elf32ResolvedSymbols& resolved_symbols) {
    const auto type = relocation.type;
    if (type == kArmRelocationNone) {
        if (relocation.symbol_index != 0) {
            throw RelocationError("R_ARM_NONE references a non-null symbol");
        }
        return addend;
    }
    if (type == kArmRelocationRelative) {
        if (relocation.symbol_index != 0) {
            throw RelocationError("R_ARM_RELATIVE references a non-null symbol");
        }
        return load_bias.Value() + addend;
    }
    if (relocation.symbol_index >= resolved_symbols.values.size()) {
        throw RelocationError("relocation symbol index is outside resolved symbols");
    }
    const auto symbol = resolved_symbols.values[relocation.symbol_index];
    if (!symbol.has_value()) {
        throw RelocationError("relocation references an unresolved symbol");
    }
    switch (type) {
        case kArmRelocationAbs32:
            return symbol->Value() + addend;
        case kArmRelocationRel32:
            return symbol->Value() + addend - runtime_target.Value();
        case kArmRelocationGlobDat:
        case kArmRelocationJumpSlot:
            return symbol->Value();
        default:
            throw RelocationError("unsupported ELF32 ARM relocation type " +
                                  std::to_string(type));
    }
}

}  // namespace

void ApplyElf32ArmRelocations(
    const Elf32RelocationTable& relocations,
    const Elf32ResolvedSymbols& resolved_symbols,
    const memory::GuestAddress load_bias, const Elf32LoadPlan& load_plan,
    memory::AddressSpace& address_space) {
    if (load_plan.load_bias != load_bias) {
        throw RelocationError("relocation load bias disagrees with load plan");
    }
    std::vector<PendingWrite> writes;
    writes.reserve(relocations.relocations.size());
    std::set<std::uint32_t> targets;
    for (const auto& relocation : relocations.relocations) {
        const auto target = Biased(relocation.target, load_bias);
        if (!targets.insert(target.Value()).second) {
            throw RelocationError("multiple relocations target the same word");
        }
        const auto original = ReadWord(address_space, target);
        writes.push_back({target, original,
                          ResolveValue(relocation, original, target, load_bias,
                                       resolved_symbols)});
    }

    std::size_t writable_regions{};
    std::size_t completed_writes{};
    try {
        for (const auto& region : load_plan.regions) {
            address_space.Protect(
                region.range, memory::PageProtection::read |
                                  memory::PageProtection::write);
            ++writable_regions;
        }
        for (const auto& write : writes) {
            WriteWord(address_space, write.target, write.value);
            ++completed_writes;
        }
        for (const auto& region : load_plan.regions) {
            address_space.Protect(region.range, region.final_protection);
        }
    } catch (...) {
        for (std::size_t index = 0; index < completed_writes; ++index) {
            WriteWord(address_space, writes[index].target, writes[index].original);
        }
        for (std::size_t index = 0; index < writable_regions; ++index) {
            address_space.Protect(load_plan.regions[index].range,
                                  load_plan.regions[index].final_protection);
        }
        throw;
    }
}

}  // namespace ogplay::loader
