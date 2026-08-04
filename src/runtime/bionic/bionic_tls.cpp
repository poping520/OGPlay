#include "ogplay/runtime/bionic/bionic_tls.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace ogplay::runtime {
namespace {

void WritePointer(memory::AddressSpace& address_space,
                  const memory::GuestAddress address,
                  const memory::GuestAddress value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value.Value() >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    address_space.Write(address, bytes);
}

}  // namespace

BionicTlsBlock CreateBionicTlsBlock(
    memory::AddressSpace& address_space,
    const memory::GuestAddress block_address,
    const memory::GuestAddress thread_info,
    const std::optional<memory::GuestAddress> preinit) {
    const auto page_size = address_space.PageSize();
    if (!block_address.IsAligned(page_size)) {
        throw std::invalid_argument("Bionic TLS block is not page aligned");
    }
    if (thread_info.Value() == 0) {
        throw std::invalid_argument("Bionic TLS thread info is null");
    }
    constexpr std::uint64_t kTlsBytes =
        static_cast<std::uint64_t>(kBionicTlsSlotCount) * 4U;
    if (kTlsBytes > page_size) {
        throw std::logic_error("Bionic TLS slots do not fit a guest page");
    }

    const memory::GuestRange mapping{block_address, page_size};
    address_space.Map(mapping, memory::PageProtection::read |
                                   memory::PageProtection::write);
    try {
        WritePointer(address_space, block_address, block_address);
        WritePointer(address_space, block_address.Add(4), thread_info);
        if (preinit.has_value()) {
            WritePointer(address_space, block_address.Add(12), *preinit);
        }
    } catch (...) {
        address_space.Unmap(mapping);
        throw;
    }
    return {mapping, block_address, thread_info};
}

void DestroyBionicTlsBlock(memory::AddressSpace& address_space,
                           const BionicTlsBlock& block) {
    if (block.thread_pointer != block.mapping.Start() ||
        block.mapping.Size() != address_space.PageSize()) {
        throw std::invalid_argument("Bionic TLS block descriptor is invalid");
    }
    address_space.Unmap(block.mapping);
}

}  // namespace ogplay::runtime
