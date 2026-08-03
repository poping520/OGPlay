#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ogplay/loader/elf.h"

namespace ogplay::loader {

inline constexpr std::uint32_t kElfProgramArmExidx = 0x70000001;
inline constexpr std::int32_t kElfDynamicInit = 12;
inline constexpr std::int32_t kElfDynamicFini = 13;
inline constexpr std::int32_t kElfDynamicInitArray = 25;
inline constexpr std::int32_t kElfDynamicFiniArray = 26;
inline constexpr std::int32_t kElfDynamicInitArraySize = 27;
inline constexpr std::int32_t kElfDynamicFiniArraySize = 28;

struct Elf32ArmExidx final {
    memory::GuestAddress address;
    std::uint32_t entry_count{};
};

struct Elf32LifecycleInfo final {
    std::optional<memory::GuestAddress> init;
    std::optional<memory::GuestAddress> fini;
    std::vector<memory::GuestAddress> init_array;
    std::vector<memory::GuestAddress> fini_array;
    std::optional<Elf32ArmExidx> arm_exidx;
};

[[nodiscard]] Elf32LifecycleInfo ReadElf32LifecycleInfo(
    std::span<const std::byte> bytes, const Elf32Image& image);

}  // namespace ogplay::loader
