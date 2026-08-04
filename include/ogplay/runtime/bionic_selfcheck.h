#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/bionic_profile.h"

namespace ogplay::runtime {

struct BionicSelfCheckReport final {
    AndroidApi api{};
    std::size_t module_count{};
    std::size_t symbol_count{};
    std::size_t relocation_count{};
    std::size_t versioned_symbol_count{};
    std::size_t lifecycle_function_count{};
    bool has_arm_exidx{};
};

[[nodiscard]] BionicSelfCheckReport SelfCheckBionicProfile(
    std::uint32_t api, std::span<const loader::Elf32ModuleInput> inputs,
    memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
