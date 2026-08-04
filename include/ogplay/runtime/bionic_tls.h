#pragma once

#include <cstdint>
#include <optional>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

inline constexpr std::uint32_t kBionicTlsSlotCount = 64;
inline constexpr std::uint32_t kBionicTlsSlotSelf = 0;
inline constexpr std::uint32_t kBionicTlsSlotThread = 1;
inline constexpr std::uint32_t kBionicTlsSlotErrno = 2;
inline constexpr std::uint32_t kBionicTlsSlotPreinit = 3;

struct BionicTlsBlock final {
    memory::GuestRange mapping;
    memory::GuestAddress thread_pointer;
    memory::GuestAddress thread_info;
};

[[nodiscard]] BionicTlsBlock CreateBionicTlsBlock(
    memory::AddressSpace& address_space, memory::GuestAddress block_address,
    memory::GuestAddress thread_info,
    std::optional<memory::GuestAddress> preinit = std::nullopt);
void DestroyBionicTlsBlock(memory::AddressSpace& address_space,
                           const BionicTlsBlock& block);

}  // namespace ogplay::runtime
