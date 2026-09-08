#pragma once

#include <cstdint>
#include <string_view>

#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/guest_process_environment.h"

namespace ogplay::runtime {

inline constexpr memory::GuestAddress kApi19GuestTlsAddress{0x6a000000U};
inline constexpr memory::GuestAddress kApi19GuestThreadInfoAddress{0x6a001000U};
inline constexpr memory::GuestAddress kApi19GuestPreinitAddress{0x6a002000U};
inline constexpr memory::GuestAddress kApi19GuestEnvironmentAddress{0x6a003000U};
inline constexpr memory::GuestAddress kApi19GuestStackAddress{0x6b000000U};
inline constexpr std::uint64_t kApi19GuestStackSize =
    UINT64_C(4) * 1024U * 1024U;
inline constexpr memory::GuestAddress kApi19GuestReturnAddress{0x6f001000U};
inline constexpr memory::GuestAddress kApi19GuestPropertyAreaAddress{0x6f002000U};

struct Api19GuestProcessRequest final {
    std::uint64_t root_thread_id{1};
    std::string_view program_name{"ogplay"};
    const GuestProcessEnvironment* environment{};
};

struct Api19GuestProcessMemory final {
    std::uint64_t root_thread_id{};
    memory::GuestAddress thread_pointer;
    memory::GuestAddress stack_top;
    memory::GuestAddress return_trap;
    memory::GuestAddress property_area;
    memory::GuestAddress environment;
};

[[nodiscard]] Api19GuestProcessMemory InitializeApi19GuestProcess(
    memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
    const loader::Elf32LinkNamespace& link_namespace,
    const Api19GuestProcessRequest& request = {});

}  // namespace ogplay::runtime
