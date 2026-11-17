#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/runtime/syscall/syscall.h"

namespace {

using namespace std::chrono_literals;
using ogplay::memory::GuestAddress;
namespace runtime = ogplay::runtime;

constexpr std::uint32_t kTable = 0x10000;
constexpr std::uint32_t kTimespec = 0x10800;
constexpr std::uint32_t kPayload = 0x10c00;
constexpr std::uint16_t kPollIn = 0x1;
constexpr std::uint16_t kPollOut = 0x4;
constexpr std::uint16_t kPollErr = 0x8;
constexpr std::uint16_t kPollHup = 0x10;
constexpr std::uint16_t kPollNval = 0x20;
constexpr std::uint16_t kPollRdNorm = 0x40;
constexpr std::uint16_t kPollWrNorm = 0x100;

struct PollFixture {
    ogplay::core::CapabilityLedger ledger;
    runtime::A32SyscallDispatcher dispatcher{runtime::CreateAndroidArmSyscallDispatcher(ledger)};
    runtime::VirtualFileSystem vfs;
    ogplay::memory::AddressSpace memory;
    runtime::GuestThreadLifecycle lifecycle;
    std::vector<std::pair<std::uint64_t, std::chrono::nanoseconds>> accounted;
    std::optional<std::chrono::nanoseconds> limit;

    PollFixture() {
        memory.Map({GuestAddress{0x10000}, memory.PageSize()},
                   ogplay::memory::PageProtection::read | ogplay::memory::PageProtection::write);
        lifecycle.Register(2);
        runtime::BindAndroidSignalSyscalls(dispatcher, memory, &lifecycle);
        runtime::BindAndroidFileSyscalls(
            dispatcher, vfs, memory, {},
            {.wait_limit = [this](std::uint64_t) { return limit; },
             .account_timeout = [this](std::uint64_t thread,
                                       std::chrono::nanoseconds timeout) { accounted.emplace_back(thread, timeout); }});
    }

    void Entry(std::size_t index, std::int32_t descriptor, std::uint16_t events) {
        const auto at = GuestAddress{kTable + static_cast<std::uint32_t>(index * 8U)};
        memory.Write32(at, static_cast<std::uint32_t>(descriptor));
        memory.Write32(at.Add(4), events | 0xbeef0000U);  // Stale revents must be overwritten.
    }

    [[nodiscard]] std::uint16_t Revents(std::size_t index) {
        return static_cast<std::uint16_t>(
            memory.Read32(GuestAddress{kTable + static_cast<std::uint32_t>(index * 8U + 4U)}) >> 16U);
    }

    runtime::A32SyscallOutcome Poll(std::uint32_t count, std::int32_t timeout_ms, std::uint32_t table = kTable) {
        runtime::A32SyscallFrame frame;
        frame.number = 168;
        frame.thread_id = 2;
        frame.arguments = {table, count, static_cast<std::uint32_t>(timeout_ms)};
        return dispatcher.DispatchOutcome(frame);
    }

    runtime::A32SyscallOutcome Ppoll(std::uint32_t count, std::uint32_t timespec, std::uint32_t sigmask = 0) {
        runtime::A32SyscallFrame frame;
        frame.number = 336;
        frame.thread_id = 2;
        frame.arguments = {kTable, count, timespec, sigmask, 8};
        return dispatcher.DispatchOutcome(frame);
    }

    void WriteByte(std::int32_t descriptor) {
        const std::array payload{std::byte{0x5a}};
        CHECK(vfs.Write(descriptor, payload) == 1U);
    }
};

}  // namespace

TEST_CASE("ARM poll reports pipe readiness without waiting") {
    PollFixture f;
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.read_descriptor, kPollIn);
    f.Entry(1, pipe.write_descriptor, kPollOut);
    f.Entry(2, -1, kPollIn);  // Negative descriptors are skipped.
    auto result = f.Poll(3, 0);
    CHECK(result.return_value == 1);
    CHECK(result.progress == runtime::SupervisorCallProgress::handled_idle);
    CHECK(f.Revents(0) == 0);
    CHECK(f.Revents(1) == kPollOut);
    CHECK(f.Revents(2) == 0);

    f.WriteByte(pipe.write_descriptor);
    f.Entry(0, pipe.read_descriptor, kPollIn | kPollRdNorm);
    CHECK(f.Poll(3, 1000).return_value == 2);
    CHECK(f.Revents(0) == (kPollIn | kPollRdNorm));
    // Only requested bits are reported.
    f.Entry(1, pipe.write_descriptor, kPollIn);
    CHECK(f.Poll(2, 0).return_value == 1);
    CHECK(f.Revents(1) == 0);
}

TEST_CASE("ARM poll wakes a blocked reader on pipe write and close") {
    PollFixture f;
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.read_descriptor, kPollIn);
    auto blocked = std::async(std::launch::async, [&] { return f.Poll(1, -1); });
    CHECK(blocked.wait_for(30ms) == std::future_status::timeout);
    const auto written = std::chrono::steady_clock::now();
    f.WriteByte(pipe.write_descriptor);
    REQUIRE(blocked.wait_for(2s) == std::future_status::ready);
    CHECK(std::chrono::steady_clock::now() - written < 500ms);
    auto result = blocked.get();
    CHECK(result.return_value == 1);
    CHECK(result.progress == runtime::SupervisorCallProgress::handled_advanced);
    CHECK(f.Revents(0) == kPollIn);

    const auto second = f.vfs.CreatePipe();
    f.Entry(0, second.read_descriptor, kPollIn);
    f.Entry(1, second.write_descriptor, kPollIn);  // Error is reported even when unrequested.
    blocked = std::async(std::launch::async, [&] { return f.Poll(1, 5000); });
    CHECK(blocked.wait_for(30ms) == std::future_status::timeout);
    f.vfs.Close(second.write_descriptor);
    REQUIRE(blocked.wait_for(2s) == std::future_status::ready);
    CHECK(blocked.get().return_value == 1);
    CHECK((f.Revents(0) & kPollHup) != 0);

    const auto third = f.vfs.CreatePipe();
    f.vfs.Close(third.read_descriptor);
    f.Entry(0, third.write_descriptor, kPollOut);
    CHECK(f.Poll(1, 0).return_value == 1);
    CHECK(f.Revents(0) == kPollErr);
}

TEST_CASE("ARM poll timeout expires, is accounted, and honours the owner wait limit") {
    PollFixture f;
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.read_descriptor, kPollIn);
    const auto start = std::chrono::steady_clock::now();
    const auto result = f.Poll(1, 40);
    CHECK(std::chrono::steady_clock::now() - start >= 40ms);
    CHECK(result.return_value == 0);
    CHECK(result.progress == runtime::SupervisorCallProgress::handled_advanced);
    CHECK(f.Revents(0) == 0);
    REQUIRE(f.accounted.size() == 1U);
    CHECK(f.accounted.front() == std::pair<std::uint64_t, std::chrono::nanoseconds>{2, 40ms});

    // The owner budget ends an infinite wait as a timeout without accounting it.
    f.limit = 20ms;
    std::future<runtime::A32SyscallOutcome> bounded = std::async(std::launch::async, [&] { return f.Poll(1, -1); });
    REQUIRE(bounded.wait_for(2s) == std::future_status::ready);
    CHECK(bounded.get().return_value == 0);
    CHECK(f.accounted.size() == 1U);
}

TEST_CASE("ARM poll classifies non-pipe, invalid and diagnostic descriptors") {
    PollFixture f;
    const auto directory = f.vfs.OpenDirectory("/");
    f.Entry(0, directory, kPollIn | kPollOut);
    f.Entry(1, 9999, 0);  // Invalid descriptors report POLLNVAL unconditionally.
    f.Entry(2, 0, kPollIn | kPollOut);
    f.Entry(3, 1, kPollIn | kPollOut);
    f.Entry(4, 2, kPollOut | kPollWrNorm);
    CHECK(f.Poll(5, 0).return_value == 5);
    CHECK(f.Revents(0) == (kPollIn | kPollOut));
    CHECK(f.Revents(1) == kPollNval);
    CHECK(f.Revents(2) == kPollIn);
    CHECK(f.Revents(3) == kPollOut);
    CHECK(f.Revents(4) == (kPollOut | kPollWrNorm));

    // A closed endpoint no longer intercepts its number.
    runtime::A32SyscallFrame close;
    close.number = 6;
    close.thread_id = 2;
    close.arguments = {1};
    REQUIRE(f.dispatcher.Dispatch(close) == 0);
    CHECK(f.Poll(4, 0).return_value == 4);
    CHECK(f.Revents(3) == kPollNval);
}

TEST_CASE("ARM poll rejects bad tables and descriptor counts") {
    PollFixture f;
    CHECK(f.Poll(1, 0, 0x30000).return_value == -14);
    CHECK(f.Poll(1025, 0).return_value == -22);
    CHECK(f.Poll(0, 0).return_value == 0);
    // Readiness is published only into a writable table.
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.write_descriptor, kPollOut);
    f.memory.Protect({GuestAddress{0x10000}, f.memory.PageSize()}, ogplay::memory::PageProtection::read);
    CHECK(f.Poll(1, 0).return_value == -14);
}

TEST_CASE("ARM poll is interrupted by pending signals and lifecycle exit") {
    PollFixture f;
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.read_descriptor, kPollIn);
    auto blocked = std::async(std::launch::async, [&] { return f.Poll(1, -1); });
    CHECK(blocked.wait_for(30ms) == std::future_status::timeout);
    f.lifecycle.RequestExit(2, 0);
    REQUIRE(blocked.wait_for(2s) == std::future_status::ready);
    const auto result = blocked.get();
    CHECK(result.return_value == -4);
    CHECK(result.progress == runtime::SupervisorCallProgress::handled_advanced);
    // Ready descriptors still win over an interruption, as on Linux.
    f.WriteByte(pipe.write_descriptor);
    CHECK(f.Poll(1, -1).return_value == 1);
}

TEST_CASE("ARM ppoll uses a checked timespec and reports the unslept time") {
    PollFixture f;
    const auto pipe = f.vfs.CreatePipe();
    f.Entry(0, pipe.read_descriptor, kPollIn);
    f.memory.Write32(GuestAddress{kTimespec}, 0);
    f.memory.Write32(GuestAddress{kTimespec + 4}, 30'000'000);
    auto result = f.Ppoll(1, kTimespec);
    CHECK(result.return_value == 0);
    CHECK(f.memory.Read32(GuestAddress{kTimespec}) == 0);
    CHECK(f.memory.Read32(GuestAddress{kTimespec + 4}) == 0);

    f.WriteByte(pipe.write_descriptor);
    f.memory.Write32(GuestAddress{kTimespec}, 5);
    f.memory.Write32(GuestAddress{kTimespec + 4}, 0);
    CHECK(f.Ppoll(1, kTimespec).return_value == 1);
    CHECK(f.memory.Read32(GuestAddress{kTimespec}) == 4);  // Nearly all of the 5 s remains.
    CHECK(f.Ppoll(1, 0).return_value == 1);                // NULL waits indefinitely but is ready.

    f.memory.Write32(GuestAddress{kTimespec + 4}, 1'000'000'000);
    CHECK(f.Ppoll(1, kTimespec).return_value == -22);
    CHECK(f.Ppoll(1, 0x30000).return_value == -14);
    // A temporary signal mask is explicitly unsupported and recorded.
    CHECK(f.Ppoll(1, 0, kPayload).return_value == -38);
}
