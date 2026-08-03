#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "ogplay/hal/virtual_memory.h"

TEST_CASE("host virtual memory reserves commits protects and decommits pages") {
    auto memory = ogplay::hal::ReserveVirtualMemory(64U * 1024U);
    REQUIRE(memory->Base() != nullptr);
    REQUIRE(memory->PageSize() != 0);
    CHECK(memory->Size() == 64U * 1024U);
    CHECK((memory->PageSize() & (memory->PageSize() - 1U)) == 0);

    const auto page = memory->PageSize();
    memory->Commit(page, page,
                   ogplay::hal::MemoryProtection::read |
                       ogplay::hal::MemoryProtection::write);
    auto* const bytes = memory->Base() + static_cast<std::size_t>(page);
    bytes[0] = std::byte{0x5a};
    CHECK(bytes[0] == std::byte{0x5a});
    memory->Protect(page, page, ogplay::hal::MemoryProtection::read);
    CHECK(bytes[0] == std::byte{0x5a});
    memory->Decommit(page, page);

    memory->Commit(page, page,
                   ogplay::hal::MemoryProtection::read |
                       ogplay::hal::MemoryProtection::write);
    CHECK(bytes[0] == std::byte{});
    memory->Decommit(page, page);
}

TEST_CASE("host virtual memory rejects invalid ranges and write-only pages") {
    CHECK_THROWS_AS(static_cast<void>(ogplay::hal::ReserveVirtualMemory(0)),
                    std::invalid_argument);
    auto memory = ogplay::hal::ReserveVirtualMemory(64U * 1024U);
    const auto page = memory->PageSize();
    CHECK_THROWS_AS(memory->Commit(1, page, ogplay::hal::MemoryProtection::read),
                    std::invalid_argument);
    CHECK_THROWS_AS(memory->Commit(0, page, ogplay::hal::MemoryProtection::write),
                    std::invalid_argument);
    CHECK_THROWS_AS(memory->Commit(memory->Size(), page,
                                   ogplay::hal::MemoryProtection::read),
                    std::invalid_argument);
}
