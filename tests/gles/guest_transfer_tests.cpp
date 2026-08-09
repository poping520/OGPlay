#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ogplay/gles/guest_transfer.h"
#include "ogplay/memory/address_space.h"

namespace {

constexpr ogplay::memory::GuestAddress kStart{0x10000};

}  // namespace

TEST_CASE("guest input transfer validates and copies an exact range") {
    ogplay::memory::AddressSpace memory;
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array source{std::byte{1}, std::byte{2}, std::byte{3}};
    memory.Write(kStart, source);

    auto buffer = ogplay::gles::GuestBuffer::Prepare(
        memory, kStart, source.size(),
        ogplay::gles::GuestTransferDirection::input, false, 17);
    CHECK_FALSE(buffer.IsNull());
    CHECK(buffer.Size() == source.size());
    CHECK(std::ranges::equal(buffer.Bytes(), source));
    CHECK_THROWS_AS(static_cast<void>(buffer.WritableBytes()),
                    ogplay::gles::GuestTransferError);
    CHECK_THROWS_AS(buffer.Commit(), ogplay::gles::GuestTransferError);
}

TEST_CASE("guest input staging reuses capacity and refreshes contents") {
    ogplay::memory::AddressSpace memory;
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array first{std::byte{1}, std::byte{2}, std::byte{3},
                           std::byte{4}};
    memory.Write(kStart, first);

    std::vector<std::byte> storage;
    const auto staged = ogplay::gles::PrepareGuestInput(
        memory, kStart, first.size(), false, storage, 17);
    REQUIRE(std::ranges::equal(staged, first));
    const auto* allocation = storage.data();

    const std::array second{std::byte{5}, std::byte{6}};
    memory.Write(kStart, second);
    const auto refreshed = ogplay::gles::PrepareGuestInput(
        memory, kStart, second.size(), false, storage, 17);
    CHECK(storage.data() == allocation);
    CHECK(std::ranges::equal(refreshed, second));
    CHECK(storage.size() == first.size());
}

TEST_CASE("guest output transfer does not read and commits explicitly") {
    ogplay::memory::AddressSpace memory;
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array original{std::byte{0xaa}, std::byte{0xaa},
                              std::byte{0xaa}, std::byte{0xaa}};
    memory.Write(kStart, original);
    auto buffer = ogplay::gles::GuestBuffer::Prepare(
        memory, kStart, 4, ogplay::gles::GuestTransferDirection::output,
        false, 23);
    auto bytes = buffer.WritableBytes();
    bytes[0] = std::byte{0x10};
    bytes[3] = std::byte{0x40};
    buffer.Commit();
    CHECK(buffer.IsCommitted());
    CHECK_THROWS_AS(buffer.Commit(), ogplay::gles::GuestTransferError);

    std::array<std::byte, 4> result{};
    memory.Read(kStart, result);
    CHECK(result == std::array{std::byte{0x10}, std::byte{0}, std::byte{0},
                               std::byte{0x40}});
}

TEST_CASE("guest inout transfer preflights both permissions") {
    ogplay::memory::AddressSpace memory;
    memory.Map({kStart, memory.PageSize()},
               ogplay::memory::PageProtection::read);
    CHECK_THROWS_AS(
        ogplay::gles::GuestBuffer::Prepare(
            memory, kStart, 8,
            ogplay::gles::GuestTransferDirection::input_output, false, 91),
        ogplay::memory::MemoryFault);

    memory.Protect({kStart, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
    auto buffer = ogplay::gles::GuestBuffer::Prepare(
        memory, kStart, 2,
        ogplay::gles::GuestTransferDirection::input_output, false);
    buffer.WritableBytes()[1] = std::byte{0x7f};
    buffer.Commit();
    CHECK(buffer.IsCommitted());
}

TEST_CASE("guest transfer handles null zero and hostile sizes explicitly") {
    ogplay::memory::AddressSpace memory;
    auto null_buffer = ogplay::gles::GuestBuffer::Prepare(
        memory, ogplay::memory::GuestAddress{}, 4096,
        ogplay::gles::GuestTransferDirection::input, true);
    CHECK(null_buffer.IsNull());
    CHECK(null_buffer.Bytes().empty());
    CHECK(null_buffer.Size() == 4096);
    CHECK_THROWS_AS(
        ogplay::gles::GuestBuffer::Prepare(
            memory, ogplay::memory::GuestAddress{}, 0,
            ogplay::gles::GuestTransferDirection::input, false),
        ogplay::gles::GuestTransferError);
    CHECK_THROWS_AS(
        ogplay::gles::GuestBuffer::Prepare(
            memory, kStart, 5, ogplay::gles::GuestTransferDirection::input,
            false, 0, 4),
        ogplay::gles::GuestTransferError);
    CHECK_THROWS_AS(
        ogplay::gles::GuestBuffer::Prepare(
            memory,
            ogplay::memory::GuestAddress{
                (std::numeric_limits<std::uint32_t>::max)() - 1U},
            4, ogplay::gles::GuestTransferDirection::input, false, 0, 4),
        std::overflow_error);
}
