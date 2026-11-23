#pragma once

#include "ogplay/runtime/common/guest_address_layout.h"

#include <cstdint>
#include <string_view>

#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/guest_process_environment.h"

namespace ogplay::runtime {

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
