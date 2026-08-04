#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "ogplay/runtime/bionic/bionic_tls.h"

namespace {

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::AddressSpace& memory,
    const ogplay::memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    memory.Read(address, bytes);
    std::uint32_t result{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

}  // namespace

TEST_CASE("Bionic TLS block initializes the legacy ARM slot contract") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress base{0x72000000U};
    const auto block = ogplay::runtime::CreateBionicTlsBlock(
        memory, base, ogplay::memory::GuestAddress{0x73000100U},
        ogplay::memory::GuestAddress{0x74000200U});
    CHECK(block.thread_pointer == base);
    CHECK(block.mapping.Size() == memory.PageSize());
    CHECK(Read32(memory, base) == base.Value());
    CHECK(Read32(memory, base.Add(4)) == 0x73000100U);
    CHECK(Read32(memory, base.Add(8)) == 0U);
    CHECK(Read32(memory, base.Add(12)) == 0x74000200U);
    CHECK(Read32(memory, base.Add(63U * 4U)) == 0U);

    ogplay::runtime::DestroyBionicTlsBlock(memory, block);
    std::array<std::byte, 1> byte{};
    CHECK_THROWS_AS(memory.Read(base, byte), ogplay::memory::MemoryFault);
}

TEST_CASE("Bionic TLS block rejects invalid or occupied mappings atomically") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress base{0x72000000U};
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::CreateBionicTlsBlock(
            memory, base.Add(4), ogplay::memory::GuestAddress{0x73000000U})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(ogplay::runtime::CreateBionicTlsBlock(
            memory, base, ogplay::memory::GuestAddress{})),
        std::invalid_argument);

    const auto block = ogplay::runtime::CreateBionicTlsBlock(
        memory, base, ogplay::memory::GuestAddress{0x73000000U});
    CHECK_THROWS(static_cast<void>(ogplay::runtime::CreateBionicTlsBlock(
        memory, base, ogplay::memory::GuestAddress{0x74000000U})));
    CHECK(Read32(memory, base.Add(4)) == 0x73000000U);
    ogplay::runtime::DestroyBionicTlsBlock(memory, block);
}
