#include "ogplay/runtime/integration/api19_guest_process.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "ogplay/runtime/bionic/bionic_tls.h"

namespace ogplay::runtime {
namespace {

constexpr std::size_t kMaximumProgramNameBytes = 31;

void Write32(memory::MemoryBus& bus, const memory::GuestAddress address,
             const std::uint32_t value, const std::uint64_t thread_id) {
    bus.Write32(address, value, thread_id);
}

void WriteString(memory::AddressSpace& address_space,
                 const memory::GuestAddress address,
                 const std::string_view value,
                 const std::uint64_t thread_id) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size() + 1);
    for (const auto character : value) {
        bytes.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    bytes.push_back(std::byte{});
    address_space.Write(address, bytes, thread_id);
}

}  // namespace

Api19GuestProcessMemory InitializeApi19GuestProcess(
    memory::AddressSpace& address_space, memory::MemoryBus& memory_bus,
    const loader::Elf32LinkNamespace& link_namespace,
    const Api19GuestProcessRequest& request) {
    if (request.root_thread_id == 0 ||
        request.root_thread_id > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(
            "API 19 guest process root thread id is outside the 32-bit ABI");
    }
    if (request.program_name.empty() ||
        request.program_name.size() > kMaximumProgramNameBytes ||
        request.program_name.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(
            "API 19 guest process program name must contain 1..31 non-null bytes");
    }

    const auto exported =
        loader::LookupElf32Symbol(link_namespace, "__system_property_area__");
    const memory::GuestRange exported_slot{exported.address,
                                            sizeof(std::uint32_t)};
    address_space.Validate(exported_slot, memory::AccessType::write,
                           request.root_thread_id);
    const auto previous_property_area =
        memory_bus.Read32(exported.address, request.root_thread_id);

    const auto page_size = address_space.PageSize();
    const auto read_write =
        memory::PageProtection::read | memory::PageProtection::write;
    const memory::GuestRange thread_info{kApi19GuestThreadInfoAddress,
                                         page_size};
    const memory::GuestRange preinit{kApi19GuestPreinitAddress, page_size};
    const memory::GuestRange stack{kApi19GuestStackAddress,
                                   kApi19GuestStackSize};
    const memory::GuestRange return_trap{kApi19GuestReturnAddress, page_size};
    const memory::GuestRange property_area{kApi19GuestPropertyAreaAddress,
                                           page_size};
    bool thread_info_mapped{};
    bool preinit_mapped{};
    bool stack_mapped{};
    bool return_mapped{};
    bool property_mapped{};
    bool export_written{};
    std::optional<BionicTlsBlock> tls;

    try {
        address_space.Map(thread_info, read_write);
        thread_info_mapped = true;
        address_space.Map(preinit, read_write);
        preinit_mapped = true;
        tls = CreateBionicTlsBlock(
            address_space, kApi19GuestTlsAddress,
            kApi19GuestThreadInfoAddress, kApi19GuestPreinitAddress);
        address_space.Map(stack, read_write);
        stack_mapped = true;
        address_space.Map(return_trap, read_write);
        return_mapped = true;
        address_space.Map(property_area, read_write);
        property_mapped = true;

        const auto thread_id =
            static_cast<std::uint32_t>(request.root_thread_id);
        const auto guest_page_size = static_cast<std::uint32_t>(page_size);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(12),
                kApi19GuestStackAddress.Value(), request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(16),
                static_cast<std::uint32_t>(kApi19GuestStackSize),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(20),
                guest_page_size, request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(32), thread_id,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestThreadInfoAddress.Add(60),
                kApi19GuestTlsAddress.Value(), request.root_thread_id);

        const auto argv = kApi19GuestPreinitAddress.Add(0x40);
        const auto envp = kApi19GuestPreinitAddress.Add(0x50);
        const auto auxv = kApi19GuestPreinitAddress.Add(0x60);
        const auto abort_message = kApi19GuestPreinitAddress.Add(0x80);
        const auto program_name = kApi19GuestPreinitAddress.Add(0xa0);
        const auto random_bytes = kApi19GuestPreinitAddress.Add(0xc0);
        Write32(memory_bus, kApi19GuestPreinitAddress, 1,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(4), argv.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(8), envp.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(12), auxv.Value(),
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPreinitAddress.Add(16),
                abort_message.Value(), request.root_thread_id);
        Write32(memory_bus, argv, program_name.Value(), request.root_thread_id);
        Write32(memory_bus, argv.Add(4), 0, request.root_thread_id);
        Write32(memory_bus, envp, 0, request.root_thread_id);
        Write32(memory_bus, auxv, 25, request.root_thread_id);
        Write32(memory_bus, auxv.Add(4), random_bytes.Value(),
                request.root_thread_id);
        Write32(memory_bus, auxv.Add(8), 6, request.root_thread_id);
        Write32(memory_bus, auxv.Add(12), guest_page_size,
                request.root_thread_id);
        Write32(memory_bus, auxv.Add(16), 0, request.root_thread_id);
        Write32(memory_bus, auxv.Add(20), 0, request.root_thread_id);
        Write32(memory_bus, abort_message, 0, request.root_thread_id);
        WriteString(address_space, program_name, request.program_name,
                    request.root_thread_id);
        Write32(memory_bus, random_bytes, 0x4f47504cU,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(4), 0x41594d34U,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(8), 0x10293847U,
                request.root_thread_id);
        Write32(memory_bus, random_bytes.Add(12), 0x56473829U,
                request.root_thread_id);

        Write32(memory_bus, kApi19GuestReturnAddress, 0xef000001U,
                request.root_thread_id);
        address_space.Protect(
            return_trap,
            memory::PageProtection::read | memory::PageProtection::execute);

        Write32(memory_bus, kApi19GuestPropertyAreaAddress, 20,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(4), 0,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(8), 0x504f5250U,
                request.root_thread_id);
        Write32(memory_bus, kApi19GuestPropertyAreaAddress.Add(12), 0xfc6ed0abU,
                request.root_thread_id);
        Write32(memory_bus, exported.address,
                kApi19GuestPropertyAreaAddress.Value(),
                request.root_thread_id);
        export_written = true;

        return {request.root_thread_id, tls->thread_pointer,
                kApi19GuestStackAddress.Add(kApi19GuestStackSize - 64U),
                kApi19GuestReturnAddress, kApi19GuestPropertyAreaAddress};
    } catch (...) {
        if (export_written) {
            try {
                Write32(memory_bus, exported.address, previous_property_area,
                        request.root_thread_id);
            } catch (...) {
            }
        }
        if (property_mapped) address_space.Unmap(property_area);
        if (return_mapped) address_space.Unmap(return_trap);
        if (stack_mapped) address_space.Unmap(stack);
        if (tls.has_value()) DestroyBionicTlsBlock(address_space, *tls);
        if (preinit_mapped) address_space.Unmap(preinit);
        if (thread_info_mapped) address_space.Unmap(thread_info);
        throw;
    }
}

}  // namespace ogplay::runtime
