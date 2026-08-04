#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "ogplay/runtime/syscall.h"

namespace {

void Write32(ogplay::memory::AddressSpace& memory,
             const ogplay::memory::GuestAddress address,
             const std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
    memory.Write(address, bytes);
}

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

TEST_CASE("ARM signal masks are checked and isolated per guest thread") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x10000U};
    memory.Map({page, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidSignalSyscalls(dispatcher, memory);
    Write32(memory, page, 0xffffffffU);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 126;
    frame.thread_id = 41;
    frame.arguments[0] = 2;
    frame.arguments[1] = page.Value();
    frame.arguments[2] = page.Add(8).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(8)) == 0);
    frame.arguments[1] = 0;
    frame.arguments[2] = page.Add(12).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK((Read32(memory, page.Add(12)) & (1U << 8U)) == 0);
    CHECK((Read32(memory, page.Add(12)) & (1U << 18U)) == 0);

    frame.thread_id = 42;
    frame.arguments[2] = page.Add(16).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(16)) == 0);
    frame.thread_id = 0;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 41;
    frame.arguments[1] = 0x20000U;
    CHECK(dispatcher.Dispatch(frame) == -14);

    frame.number = 175;
    frame.arguments[1] = 0;
    frame.arguments[2] = page.Add(24).Value();
    frame.arguments[3] = 4;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[3] = 8;
    CHECK(dispatcher.Dispatch(frame) == 0);
}

TEST_CASE("ARM sigaltstack stores enabled and disabled thread state") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x10000U};
    memory.Map({page, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidSignalSyscalls(dispatcher, memory);
    Write32(memory, page, 0x60000000U);
    Write32(memory, page.Add(4), 0);
    Write32(memory, page.Add(8), 8192);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 186;
    frame.thread_id = 51;
    frame.arguments[0] = page.Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[0] = 0;
    frame.arguments[1] = page.Add(16).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(16)) == 0x60000000U);
    CHECK(Read32(memory, page.Add(20)) == 0);
    CHECK(Read32(memory, page.Add(24)) == 8192);

    Write32(memory, page.Add(4), 2);
    frame.arguments[0] = page.Value();
    frame.arguments[1] = 0;
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[0] = 0;
    frame.arguments[1] = page.Add(32).Value();
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(Read32(memory, page.Add(36)) == 2);
    frame.arguments[0] = 0x20000U;
    CHECK(dispatcher.Dispatch(frame) == -14);
}
