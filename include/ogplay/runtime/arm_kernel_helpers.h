#pragma once

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

inline constexpr memory::GuestAddress kArmKernelHelperPage{0xffff0000U};
inline constexpr memory::GuestAddress kArmKernelMemoryBarrier{0xffff0fa0U};
inline constexpr memory::GuestAddress kArmKernelCmpxchg{0xffff0fc0U};
inline constexpr memory::GuestAddress kArmKernelGetTls{0xffff0fe0U};

void MapArmKernelHelpers(memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
