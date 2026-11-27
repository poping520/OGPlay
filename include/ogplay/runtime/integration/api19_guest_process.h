#pragma once

#include "ogplay/runtime/common/guest_address_layout.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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

// Writes the properties into the process's bionic prop_area as the API 19
// trie (bionic/libc/bionic/system_properties.c), so guest __system_property_get
// reads the same values as Java SystemProperties. Replaces any earlier set;
// names must be 1..31 bytes of non-empty dot-separated segments and values at
// most 91 bytes. Fails without writing when the set does not fit.
void PublishApi19SystemProperties(memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
                                  memory::GuestAddress property_area,
                                  std::span<const std::pair<std::string, std::string>> properties,
                                  std::uint64_t thread_id);

}  // namespace ogplay::runtime
