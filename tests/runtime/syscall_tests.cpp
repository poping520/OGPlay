#include <doctest/doctest.h>

#include <cstdint>
#include <array>
#include <cstddef>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/syscall/syscall.h"

TEST_CASE("Android ARM syscall baseline exposes identity and coverage") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher = ogplay::runtime::CreateAndroidArmSyscallDispatcher(
        ledger, {.process_id = 42, .user_id = 10001, .group_id = 10002});
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 20;
    CHECK(dispatcher.Dispatch(frame) == 42);
    frame.number = 199;
    CHECK(dispatcher.Dispatch(frame) == 10001);
    frame.number = 200;
    CHECK(dispatcher.Dispatch(frame) == 10002);
    frame.number = 224;
    frame.thread_id = 77;
    CHECK(dispatcher.Dispatch(frame) == 77);
    const auto coverage = dispatcher.Coverage();
    CHECK(coverage.declared >= 120);
    CHECK(coverage.implemented == 6);
    CHECK(coverage.declared_by_group.at(
              ogplay::runtime::SyscallGroup::file) > 20);
}

TEST_CASE("syscall progress table locks I/O and conservative defaults") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    dispatcher.Implement(3U, [](const ogplay::runtime::A32SyscallFrame&) {
        return 1;
    });
    dispatcher.Implement(4U, [](const ogplay::runtime::A32SyscallFrame&) {
        return 1;
    });
    dispatcher.Implement(180U, [](const ogplay::runtime::A32SyscallFrame&) {
        return 1;
    });
    dispatcher.Implement(181U, [](const ogplay::runtime::A32SyscallFrame&) {
        return 1;
    });
    ogplay::runtime::A32SyscallFrame frame;
    for (const auto number : {3U, 4U, 180U, 181U}) {
        frame.number = number;
        CHECK(dispatcher.DispatchOutcome(frame).progress ==
              ogplay::runtime::SupervisorCallProgress::handled_advanced);
    }
    frame.number = 20U;  // getpid
    CHECK(dispatcher.DispatchOutcome(frame).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
}

TEST_CASE("extended Android ARM syscall directory remains explicitly observable") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 220;
    frame.link_register = 0x2200U;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].id == "syscall.madvise");
    CHECK(hits[0].first_lr == 0x2200U);
    const auto coverage = dispatcher.Coverage();
    CHECK(coverage.declared >= 120);
    CHECK(coverage.declared_by_group.at(
              ogplay::runtime::SyscallGroup::signal) >= 8);
}

TEST_CASE("declared and unknown syscalls return ENOSYS and are observable") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 3;
    frame.link_register = 0x1234;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    frame.number = 999;
    frame.link_register = 0x5678;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].id == "syscall.arm.999");
    CHECK(hits[0].first_lr == 0x5678);
    CHECK(hits[1].id == "syscall.read");
    CHECK(hits[1].first_lr == 0x1234);
}

TEST_CASE("syscall declarations reject duplicate numbers and empty handlers") {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher{ledger};
    dispatcher.Declare(1, "first", ogplay::runtime::SyscallGroup::process);
    CHECK_THROWS_AS(
        dispatcher.Declare(1, "second", ogplay::runtime::SyscallGroup::process),
        ogplay::runtime::SyscallError);
    CHECK_THROWS_AS(
        dispatcher.Register(2, "empty", ogplay::runtime::SyscallGroup::process,
                            {}),
        ogplay::runtime::SyscallError);
}

TEST_CASE("syscall observer sees implemented and unimplemented results") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    std::vector<std::pair<std::uint32_t, std::int32_t>> observed;
    dispatcher.SetObserver(
        [&observed](const ogplay::runtime::A32SyscallFrame& frame,
                    const std::int32_t result) {
            observed.emplace_back(frame.number, result);
        });
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 20;
    CHECK(dispatcher.Dispatch(frame) == 1000);
    frame.number = 999;
    CHECK(dispatcher.Dispatch(frame) == -ogplay::runtime::kLinuxEnosys);
    CHECK(observed ==
          std::vector<std::pair<std::uint32_t, std::int32_t>>{
              {20, 1000}, {999, -ogplay::runtime::kLinuxEnosys}});
}

TEST_CASE("Android time syscalls use the unified clock and checked guest memory") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::hal::FixedStepClock clock{100, 1000};
    clock.AdvanceFrames(12);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange page{
        ogplay::memory::GuestAddress{0x10000}, memory.PageSize()};
    memory.Map(page, ogplay::memory::PageProtection::read |
                         ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidTimeSyscalls(dispatcher, clock, memory);

    const auto read32 = [&memory](const std::uint32_t address) {
        std::array<std::byte, 4> bytes{};
        memory.Read(ogplay::memory::GuestAddress{address}, bytes);
        std::uint32_t value{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            value |= static_cast<std::uint32_t>(
                         std::to_integer<std::uint8_t>(bytes[index]))
                     << static_cast<unsigned>(index * 8U);
        }
        return value;
    };

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 263;
    frame.arguments[0] = 1;
    frame.arguments[1] = 0x10000;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(read32(0x10000) == 1);
    CHECK(read32(0x10004) == 200000000);

    frame.number = 78;
    frame.arguments[0] = 0x10010;
    frame.arguments[1] = 0x10020;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(read32(0x10010) == 1);
    CHECK(read32(0x10014) == 200000);
    CHECK(read32(0x10020) == 0);

    const auto write32 = [&memory](const std::uint32_t address,
                                   const std::uint32_t value) {
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        memory.Write(ogplay::memory::GuestAddress{address}, bytes);
    };
    frame.number = 162;
    frame.arguments[0] = 0x10030;
    frame.arguments[1] = 0;
    write32(0x10030, 0U);
    write32(0x10034, 0U);
    CHECK(dispatcher.DispatchOutcome(frame).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    write32(0x10034, 1U);
    CHECK(dispatcher.DispatchOutcome(frame).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);

    frame.number = 263;
    frame.arguments[0] = 99;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[0] = 1;
    frame.arguments[1] = 0x20000;
    CHECK(dispatcher.Dispatch(frame) == -14);
}

TEST_CASE("Android memory syscalls map protect unmap and grow brk safely") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    ogplay::runtime::BindAndroidMemorySyscalls(dispatcher, memory);
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 192;
    frame.arguments[1] = 5000;
    frame.arguments[2] = 3;
    frame.arguments[3] = 0x22;
    frame.arguments[4] = UINT32_MAX;
    CHECK(dispatcher.Dispatch(frame) == 0x60000000);
    const std::array marker{std::byte{0x5a}};
    CHECK_NOTHROW(memory.Write(ogplay::memory::GuestAddress{0x60000000}, marker));

    frame.number = 125;
    frame.arguments[0] = 0x60000000;
    frame.arguments[1] = 5000;
    frame.arguments[2] = 5;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK_THROWS_AS(memory.Write(ogplay::memory::GuestAddress{0x60000000}, marker),
                    ogplay::memory::MemoryFault);
    frame.arguments[2] = 6;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK_NOTHROW(memory.Write(ogplay::memory::GuestAddress{0x60000000}, marker));
    std::array<std::byte, 1> fetched{};
    CHECK_NOTHROW(memory.Fetch(ogplay::memory::GuestAddress{0x60000000},
                               fetched));

    frame.number = 91;
    frame.arguments[0] = 0x60000000;
    frame.arguments[1] = 5000;
    CHECK(dispatcher.Dispatch(frame) == 0);
    std::array<std::byte, 1> output{};
    CHECK_THROWS_AS(memory.Read(ogplay::memory::GuestAddress{0x60000000}, output),
                    ogplay::memory::MemoryFault);

    frame.number = 45;
    frame.arguments[0] = 0;
    CHECK(dispatcher.Dispatch(frame) == 0x50000000);
    frame.arguments[0] = 0x50001010;
    CHECK(dispatcher.Dispatch(frame) == 0x50001010);
    CHECK_NOTHROW(memory.Write(ogplay::memory::GuestAddress{0x50000000}, marker));
}

TEST_CASE("Android madvise validates hints and discards writable pages") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress page{0x12000U};
    memory.Map({page, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array marker{std::byte{0x5a}, std::byte{0x6b}};
    memory.Write(page.Add(32), marker);
    ogplay::runtime::BindAndroidMemorySyscalls(dispatcher, memory);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 220;
    frame.thread_id = 71;
    frame.arguments[0] = page.Value();
    frame.arguments[1] = static_cast<std::uint32_t>(memory.PageSize());
    frame.arguments[2] = 12;
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[2] = 4;
    CHECK(dispatcher.Dispatch(frame) == 0);
    std::array<std::byte, 2> discarded{};
    memory.Read(page.Add(32), discarded);
    CHECK(discarded == std::array<std::byte, 2>{});

    frame.arguments[0] = page.Value() + 1U;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[0] = 0x22000U;
    CHECK(dispatcher.Dispatch(frame) == -12);
    frame.arguments[0] = page.Value();
    frame.arguments[1] = 0;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[1] = 1;
    frame.arguments[2] = 16;
    CHECK(dispatcher.Dispatch(frame) == -22);

    frame.number = 125;
    frame.arguments[0] = page.Value();
    frame.arguments[1] = static_cast<std::uint32_t>(memory.PageSize());
    frame.arguments[2] = 0;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK_THROWS_AS(memory.Read(page, discarded),
                    ogplay::memory::MemoryFault);
}

TEST_CASE("Android futex syscall waits wakes and reports Linux errors") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange page{
        ogplay::memory::GuestAddress{0x10000}, memory.PageSize()};
    memory.Map(page, ogplay::memory::PageProtection::read |
                         ogplay::memory::PageProtection::write);
    ogplay::memory::CheckedMemoryBus bus{memory};
    bus.Write32(ogplay::memory::GuestAddress{0x10000}, 7);
    ogplay::cpu::FutexTable futex;
    ogplay::runtime::BindAndroidThreadSyscalls(dispatcher, futex, bus);

    ogplay::runtime::A32SyscallFrame mismatch;
    mismatch.number = 240;
    mismatch.arguments[0] = 0x10000;
    mismatch.arguments[1] = 0;
    mismatch.arguments[2] = 8;
    const auto mismatch_outcome = dispatcher.DispatchOutcome(mismatch);
    CHECK(mismatch_outcome.return_value == -11);
    CHECK(mismatch_outcome.progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);

    bus.Write32(ogplay::memory::GuestAddress{0x10020}, 0U);
    bus.Write32(ogplay::memory::GuestAddress{0x10024}, 0U);
    auto timed = mismatch;
    timed.arguments[2] = 7;
    timed.arguments[3] = 0x10020;
    CHECK(dispatcher.DispatchOutcome(timed).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    bus.Write32(ogplay::memory::GuestAddress{0x10024}, 1U);
    CHECK(dispatcher.DispatchOutcome(timed).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);

    std::atomic<std::int32_t> wait_result{-999};
    std::thread waiter{[&] {
        auto wait = mismatch;
        wait.arguments[2] = 7;
        wait.thread_id = 42;
        wait_result = dispatcher.Dispatch(wait);
    }};
    for (std::size_t attempt = 0;
         attempt < 100000 &&
         futex.WaiterCount(ogplay::memory::GuestAddress{0x10000}) == 0;
         ++attempt) {
        std::this_thread::yield();
    }
    REQUIRE(futex.WaiterCount(ogplay::memory::GuestAddress{0x10000}) == 1);
    auto wake = mismatch;
    wake.arguments[1] = 1 | 128;
    wake.arguments[2] = 1;
    const auto wake_outcome = dispatcher.DispatchOutcome(wake);
    CHECK(wake_outcome.return_value == 1);
    CHECK(wake_outcome.progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    waiter.join();
    CHECK(wait_result == 0);

    std::atomic<ogplay::runtime::SupervisorCallProgress> interrupted_progress{
        ogplay::runtime::SupervisorCallProgress::handled_idle};
    std::thread interrupted_waiter{[&] {
        auto wait = mismatch;
        wait.arguments[2] = 7;
        wait.thread_id = 43;
        const auto outcome = dispatcher.DispatchOutcome(wait);
        wait_result = outcome.return_value;
        interrupted_progress = outcome.progress;
    }};
    for (std::size_t attempt = 0;
         attempt < 100000 &&
         futex.WaiterCount(ogplay::memory::GuestAddress{0x10000}) == 0;
         ++attempt) {
        std::this_thread::yield();
    }
    REQUIRE(futex.WaiterCount(ogplay::memory::GuestAddress{0x10000}) == 1);
    CHECK(futex.InterruptAll() == 1);
    interrupted_waiter.join();
    CHECK(wait_result == -4);
    CHECK(interrupted_progress ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);
    CHECK(dispatcher.Dispatch(mismatch) == -11);
    mismatch.arguments[2] = 7;
    const auto preinterrupted = dispatcher.DispatchOutcome(mismatch);
    CHECK(preinterrupted.return_value == -4);
    CHECK(preinterrupted.progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);

    mismatch.arguments[0] = 0x10001;
    CHECK(dispatcher.Dispatch(mismatch) == -22);
    mismatch.arguments[0] = 0x20000;
    CHECK(dispatcher.Dispatch(mismatch) == -14);
    mismatch.arguments[0] = 0x10000;
    mismatch.arguments[3] = 0x20000;
    CHECK(dispatcher.Dispatch(mismatch) == -14);
}

TEST_CASE("Android file syscalls transfer checked guest bytes through VFS") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array initial{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    vfs.PutFile("/data/sample.txt", initial, true);
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange page{
        ogplay::memory::GuestAddress{0x10000}, memory.PageSize()};
    memory.Map(page, ogplay::memory::PageProtection::read |
                         ogplay::memory::PageProtection::write);
    const auto write_string = [&memory](const std::uint32_t address,
                                        const char* value) {
        std::vector<std::byte> bytes;
        do {
            bytes.push_back(static_cast<std::byte>(*value));
        } while (*value++ != '\0');
        memory.Write(ogplay::memory::GuestAddress{address}, bytes);
    };
    write_string(0x10000, "/DATA/SAMPLE.TXT");
    ogplay::runtime::BindAndroidFileSyscalls(dispatcher, vfs, memory);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 5;
    frame.arguments[0] = 0x10000;
    frame.arguments[1] = 2;
    const auto descriptor = dispatcher.Dispatch(frame);
    REQUIRE(descriptor >= 3);
    frame.number = 3;
    frame.arguments[0] = static_cast<std::uint32_t>(descriptor);
    frame.arguments[1] = 0x10100;
    frame.arguments[2] = 3;
    const auto read_outcome = dispatcher.DispatchOutcome(frame);
    CHECK(read_outcome.return_value == 3);
    CHECK(read_outcome.progress ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);
    CHECK(dispatcher.DispatchOutcome(frame).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);  // EOF
    std::array<std::byte, 3> output{};
    memory.Read(ogplay::memory::GuestAddress{0x10100}, output);
    CHECK(output == initial);

    frame.number = 19;
    frame.arguments[1] = 0;
    frame.arguments[2] = 0;
    CHECK(dispatcher.Dispatch(frame) == 0);
    const std::array replacement{std::byte{'x'}, std::byte{'y'}};
    memory.Write(ogplay::memory::GuestAddress{0x10110}, replacement);
    frame.number = 4;
    frame.arguments[1] = 0x10110;
    frame.arguments[2] = 2;
    const auto write_outcome = dispatcher.DispatchOutcome(frame);
    CHECK(write_outcome.return_value == 2);
    CHECK(write_outcome.progress ==
          ogplay::runtime::SupervisorCallProgress::handled_advanced);
    frame.arguments[2] = 0;
    CHECK(dispatcher.DispatchOutcome(frame).progress ==
          ogplay::runtime::SupervisorCallProgress::handled_idle);
    frame.number = 6;
    CHECK(dispatcher.Dispatch(frame) == 0);

    const auto verify = vfs.Open("/data/sample.txt", {.read = true});
    std::array<std::byte, 3> actual{};
    CHECK(vfs.Read(verify, actual) == 3);
    CHECK(actual[0] == std::byte{'x'});
    CHECK(actual[1] == std::byte{'y'});
    vfs.Close(verify);

    frame.number = 5;
    frame.arguments[0] = 0;
    CHECK(dispatcher.Dispatch(frame) == -14);
}

TEST_CASE("Android pipe syscall publishes a working descriptor pair") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::memory::AddressSpace memory;
    memory.Map({ogplay::memory::GuestAddress{0x10000}, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::BindAndroidFileSyscalls(dispatcher, vfs, memory);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 42;
    frame.thread_id = 7;
    frame.arguments[0] = 0x10000;
    CHECK(dispatcher.Dispatch(frame) == 0);
    std::array<std::byte, 8> descriptor_bytes{};
    memory.Read(ogplay::memory::GuestAddress{0x10000}, descriptor_bytes);
    const auto read_descriptor =
        std::to_integer<std::uint8_t>(descriptor_bytes[0]);
    const auto write_descriptor =
        std::to_integer<std::uint8_t>(descriptor_bytes[4]);
    REQUIRE(read_descriptor >= 3);
    REQUIRE(write_descriptor > read_descriptor);

    const std::array payload{std::byte{0x55}};
    memory.Write(ogplay::memory::GuestAddress{0x10100}, payload);
    frame.number = 4;
    frame.arguments[0] = write_descriptor;
    frame.arguments[1] = 0x10100;
    frame.arguments[2] = 1;
    CHECK(dispatcher.Dispatch(frame) == 1);
    frame.number = 3;
    frame.arguments[0] = read_descriptor;
    frame.arguments[1] = 0x10200;
    CHECK(dispatcher.Dispatch(frame) == 1);
    std::array<std::byte, 1> output{};
    memory.Read(ogplay::memory::GuestAddress{0x10200}, output);
    CHECK(output == payload);

    frame.number = 42;
    frame.arguments[0] = 0x20000;
    CHECK(dispatcher.Dispatch(frame) == -14);
}

TEST_CASE("ARM set_tls updates only the current guest thread pointer") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::cpu::A32State state;
    state.SetThreadId(42);
    ogplay::memory::AddressSpace memory;
    memory.Map({ogplay::memory::GuestAddress{0x12000U}, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write |
                   ogplay::memory::PageProtection::execute);
    ogplay::runtime::BindAndroidArmPrivateSyscalls(
        dispatcher, memory, [&state](const std::uint64_t thread_id,
                             const ogplay::memory::GuestAddress value) {
            if (thread_id != state.ThreadId()) return false;
            if (value.Value() == UINT32_MAX) {
                throw std::invalid_argument("reserved test pointer");
            }
            state.SetThreadPointer(value);
            return true;
        });

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 0x0f0005U;
    frame.thread_id = 42;
    frame.arguments[0] = 0x72000000U;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(state.ThreadPointer() ==
          ogplay::memory::GuestAddress{0x72000000U});
    frame.number = 0x0f0002U;
    frame.arguments = {0x12010U, 0x12020U, 0U};
    CHECK(dispatcher.Dispatch(frame) == 0);
    frame.arguments[0] = 0x22000U;
    frame.arguments[1] = 0x22010U;
    CHECK(dispatcher.Dispatch(frame) == -14);
    frame.arguments[0] = 0x12020U;
    frame.arguments[1] = 0x12010U;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.number = 0x0f0005U;
    frame.thread_id = 43;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 0;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 42;
    frame.arguments[0] = UINT32_MAX;
    CHECK(dispatcher.Dispatch(frame) == -22);
    CHECK_THROWS_AS(
        ogplay::runtime::BindAndroidArmPrivateSyscalls(dispatcher, memory, {}),
        ogplay::runtime::SyscallError);
}

TEST_CASE("thread lifecycle syscalls request exit and retain clear child tid") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(41);
    lifecycle.Register(42);
    ogplay::runtime::BindAndroidThreadLifecycleSyscalls(dispatcher, lifecycle);

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 256;
    frame.thread_id = 42;
    frame.arguments[0] = 0x10020U;
    CHECK(dispatcher.Dispatch(frame) == 42);
    CHECK(lifecycle.State(42).clear_child_tid ==
          ogplay::memory::GuestAddress{0x10020U});
    frame.number = 1;
    frame.arguments[0] = 3;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(lifecycle.State(42).status ==
          ogplay::runtime::GuestThreadStatus::exit_requested);
    CHECK(lifecycle.State(42).exit_code == 3);

    frame.number = 248;
    frame.thread_id = 41;
    frame.arguments[0] = 9;
    CHECK(dispatcher.Dispatch(frame) == 0);
    CHECK(lifecycle.State(41).exit_code == 9);
    CHECK(lifecycle.State(42).exit_code == 9);
    frame.thread_id = 99;
    CHECK(dispatcher.Dispatch(frame) == -3);
}

TEST_CASE("ARM clone decodes pthread arguments at an explicit spawn boundary") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    std::optional<ogplay::runtime::GuestThreadCloneRequest> captured;
    ogplay::runtime::BindAndroidCloneSyscall(
        dispatcher,
        [&captured](const ogplay::runtime::GuestThreadCloneRequest& request) {
            captured = request;
            return 52;
        });

    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 120;
    frame.thread_id = 41;
    ogplay::cpu::A32State parent_state;
    parent_state.SetThreadId(41);
    parent_state.SetRegister(ogplay::cpu::CoreRegister::pc, 0x20004U);
    parent_state.SetCpsr(0x20U);
    frame.cpu_state = parent_state;
    frame.arguments[0] =
        ogplay::runtime::kLinuxCloneVm | ogplay::runtime::kLinuxCloneFs |
        ogplay::runtime::kLinuxCloneFiles |
        ogplay::runtime::kLinuxCloneSighand |
        ogplay::runtime::kLinuxCloneThread |
        ogplay::runtime::kLinuxCloneSysvsem |
        ogplay::runtime::kLinuxCloneSettls |
        ogplay::runtime::kLinuxCloneParentSettid |
        ogplay::runtime::kLinuxCloneChildCleartid;
    frame.arguments[1] = 0x71001000U;
    frame.arguments[2] = 0x10020U;
    frame.arguments[3] = 0x72000000U;
    frame.arguments[4] = 0x10024U;
    CHECK(dispatcher.Dispatch(frame) == 52);
    REQUIRE(captured.has_value());
    CHECK(captured->parent_thread_id == 41);
    CHECK(captured->child_stack ==
          ogplay::memory::GuestAddress{0x71001000U});
    CHECK(captured->parent_tid ==
          ogplay::memory::GuestAddress{0x10020U});
    CHECK(captured->thread_pointer ==
          ogplay::memory::GuestAddress{0x72000000U});
    CHECK(captured->child_tid ==
          ogplay::memory::GuestAddress{0x10024U});
    CHECK(captured->parent_cpu_state.Register(
              ogplay::cpu::CoreRegister::pc) == 0x20004U);
    CHECK(captured->parent_cpu_state.State() ==
          ogplay::cpu::ExecutionState::thumb);
}

TEST_CASE("ARM clone rejects unsupported shapes before spawning") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    std::size_t calls{};
    ogplay::runtime::BindAndroidCloneSyscall(
        dispatcher,
        [&calls](const ogplay::runtime::GuestThreadCloneRequest&) {
            ++calls;
            return 0;
        });
    ogplay::runtime::A32SyscallFrame frame;
    frame.number = 120;
    frame.thread_id = 41;
    ogplay::cpu::A32State parent_state;
    parent_state.SetThreadId(41);
    frame.cpu_state = parent_state;
    frame.arguments[0] =
        ogplay::runtime::kLinuxCloneVm | ogplay::runtime::kLinuxCloneFs |
        ogplay::runtime::kLinuxCloneFiles |
        ogplay::runtime::kLinuxCloneSighand;
    frame.arguments[1] = 0x71001000U;
    CHECK(dispatcher.Dispatch(frame) == -95);
    frame.arguments[0] |= ogplay::runtime::kLinuxCloneThread;
    frame.arguments[1] = 0x71001004U;
    CHECK(dispatcher.Dispatch(frame) == -22);
    frame.arguments[1] = 0x71001000U;
    frame.arguments[0] |= ogplay::runtime::kLinuxCloneParentSettid;
    frame.arguments[2] = 0x10001U;
    CHECK(dispatcher.Dispatch(frame) == -22);
    CHECK(calls == 0);
    frame.arguments[0] &= ~ogplay::runtime::kLinuxCloneParentSettid;
    frame.arguments[1] = 0x71001000U;
    frame.cpu_state.reset();
    CHECK(dispatcher.Dispatch(frame) == -22);
    CHECK_THROWS_AS(ogplay::runtime::BindAndroidCloneSyscall(dispatcher, {}),
                    ogplay::runtime::SyscallError);
}

TEST_CASE("clone commit publishes Bionic TID TLS and lifecycle state") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress tid{0x10020U};
    memory.Map({ogplay::memory::GuestAddress{0x10000U}, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(41);
    ogplay::runtime::GuestThreadCloneCommitter committer{lifecycle, memory};
    ogplay::runtime::GuestThreadCloneRequest request;
    request.parent_thread_id = 41;
    request.flags = ogplay::runtime::kLinuxCloneParentSettid |
                    ogplay::runtime::kLinuxCloneSettls |
                    ogplay::runtime::kLinuxCloneChildCleartid;
    request.child_stack = ogplay::memory::GuestAddress{0x71001000U};
    request.parent_tid = tid;
    request.thread_pointer = ogplay::memory::GuestAddress{0x72000000U};
    request.child_tid = tid;
    CHECK(committer.Commit(request, 52) == 52);
    std::array<std::byte, 4> stored{};
    memory.Read(tid, stored);
    const std::array expected{std::byte{52}, std::byte{0},
                              std::byte{0}, std::byte{0}};
    CHECK(stored == expected);
    const auto child = lifecycle.State(52);
    CHECK(child.thread_pointer ==
          ogplay::memory::GuestAddress{0x72000000U});
    CHECK(child.clear_child_tid == tid);
}

TEST_CASE("clone commit preflights all TID writes and rejects partial state") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestAddress parent_tid{0x10020U};
    memory.Map({ogplay::memory::GuestAddress{0x10000U}, memory.PageSize()},
               ogplay::memory::PageProtection::read |
                   ogplay::memory::PageProtection::write);
    const std::array original{std::byte{0x63}, std::byte{0},
                              std::byte{0}, std::byte{0}};
    memory.Write(parent_tid, original);
    ogplay::runtime::GuestThreadLifecycle lifecycle;
    lifecycle.Register(61);
    ogplay::runtime::GuestThreadCloneCommitter committer{lifecycle, memory};
    ogplay::runtime::GuestThreadCloneRequest request;
    request.parent_thread_id = 61;
    request.flags = ogplay::runtime::kLinuxCloneParentSettid |
                    ogplay::runtime::kLinuxCloneChildSettid;
    request.parent_tid = parent_tid;
    request.child_tid = ogplay::memory::GuestAddress{0x20000U};
    CHECK(committer.Commit(request, 62) == -14);
    std::array<std::byte, 4> stored{};
    memory.Read(parent_tid, stored);
    CHECK(stored == original);
    CHECK_THROWS_AS(static_cast<void>(lifecycle.State(62)),
                    ogplay::runtime::GuestThreadLifecycleError);

    request.flags = ogplay::runtime::kLinuxCloneSettls;
    request.parent_tid.reset();
    request.child_tid.reset();
    CHECK(committer.Commit(request, 62) == -22);
    lifecycle.RequestExit(61, 0);
    request.thread_pointer = ogplay::memory::GuestAddress{0x72000000U};
    CHECK(committer.Commit(request, 62) == -3);
}

// ---- file metadata and directory syscalls (SBX-4, ADR-0020) --------------

namespace {

using ogplay::memory::GuestAddress;

// One guest page of scratch, one dispatcher with both file syscall batches.
struct MetadataSyscallFixture final {
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::A32SyscallDispatcher dispatcher;
    ogplay::runtime::VirtualFileSystem vfs;
    ogplay::memory::AddressSpace memory;

    MetadataSyscallFixture()
        : dispatcher(
              ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger)) {
        memory.Map({GuestAddress{0x20000}, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        ogplay::runtime::BindAndroidFileSyscalls(dispatcher, vfs, memory);
        ogplay::runtime::BindAndroidFileMetadataSyscalls(dispatcher, vfs,
                                                         memory);
    }

    void WriteString(const std::uint32_t address, const std::string& value) {
        std::vector<std::byte> bytes;
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        bytes.push_back(std::byte{0});
        memory.Write(GuestAddress{address}, bytes);
    }

    [[nodiscard]] std::int32_t Call(const std::uint32_t number,
                                    const std::array<std::uint32_t, 6>& args) {
        ogplay::runtime::A32SyscallFrame frame;
        frame.number = number;
        frame.thread_id = 1;
        for (std::size_t index = 0; index < args.size(); ++index) {
            frame.arguments[index] = args[index];
        }
        return dispatcher.Dispatch(frame);
    }

    [[nodiscard]] std::uint64_t ReadField(const std::uint32_t address,
                                          const std::size_t offset,
                                          const std::size_t width) {
        std::vector<std::byte> bytes(width);
        memory.Read(GuestAddress{address + static_cast<std::uint32_t>(offset)},
                    bytes);
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < width; ++index) {
            value |= static_cast<std::uint64_t>(
                         std::to_integer<std::uint8_t>(bytes[index]))
                     << static_cast<unsigned>(index * 8U);
        }
        return value;
    }
};

}  // namespace

TEST_CASE("Android mkdir, stat64 and unlink syscalls reach the VFS") {
    MetadataSyscallFixture fixture;
    // mkdir does not imply -p: the parent has to exist first.
    fixture.WriteString(0x20000, "/sdcard/saves");
    CHECK(fixture.Call(39, {0x20000, 0755, 0, 0, 0, 0}) == -2);
    fixture.vfs.CreateDirectory("/sdcard");
    CHECK(fixture.Call(39, {0x20000, 0755, 0, 0, 0, 0}) == 0);
    CHECK(fixture.vfs.Stat("/sdcard/saves").is_directory);
    // Creating it twice is -EEXIST, exactly as on the platform.
    CHECK(fixture.Call(39, {0x20000, 0755, 0, 0, 0, 0}) == -17);

    // struct stat64 layout is the Android ARM one; these offsets are the
    // contract, not an implementation detail. ARM does not pack the struct,
    // so the 64-bit members sit on eight-byte boundaries: the guest libc
    // __swhatbuf reads st_mode at 16 and st_blksize at 56 out of a 104-byte
    // frame. The packed x86 offsets would put our st_blksize in the high
    // word of the guest's st_size.
    CHECK(fixture.Call(195, {0x20000, 0x20200, 0, 0, 0, 0}) == 0);
    CHECK((fixture.ReadField(0x20200, 16, 4) & 0170000U) == 0040000U);
    CHECK(fixture.ReadField(0x20200, 20, 4) == 1);       // st_nlink
    CHECK(fixture.ReadField(0x20200, 48, 8) == 0);       // st_size
    CHECK(fixture.ReadField(0x20200, 56, 4) == 4096);    // st_blksize
    CHECK(fixture.ReadField(0x20200, 96, 8) != 0);       // st_ino
    // The padding before st_size stays clear, so a guest reading the 64-bit
    // st_size cannot pick up a neighbouring field as its high word.
    CHECK(fixture.ReadField(0x20200, 44, 4) == 0);

    fixture.WriteString(0x20100, "/sdcard/saves/slot0.sav");
    const auto descriptor =
        fixture.Call(5, {0x20100, 0x40 | 1, 0, 0, 0, 0});  // O_CREAT|O_WRONLY
    REQUIRE(descriptor >= 3);
    const std::array payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    fixture.memory.Write(GuestAddress{0x20300}, payload);
    CHECK(fixture.Call(4, {static_cast<std::uint32_t>(descriptor), 0x20300, 3,
                           0, 0, 0}) == 3);
    CHECK(fixture.Call(118, {static_cast<std::uint32_t>(descriptor), 0, 0, 0,
                             0, 0}) == 0);  // fsync
    CHECK(fixture.Call(6, {static_cast<std::uint32_t>(descriptor), 0, 0, 0, 0,
                           0}) == 0);

    CHECK(fixture.Call(195, {0x20100, 0x20200, 0, 0, 0, 0}) == 0);
    CHECK((fixture.ReadField(0x20200, 16, 4) & 0170000U) == 0100000U);
    CHECK(fixture.ReadField(0x20200, 48, 8) == 3);

    // Non-empty directory refuses, the file goes, then the directory goes.
    CHECK(fixture.Call(40, {0x20000, 0, 0, 0, 0, 0}) == -39);
    CHECK(fixture.Call(10, {0x20100, 0, 0, 0, 0, 0}) == 0);
    CHECK(fixture.Call(40, {0x20000, 0, 0, 0, 0, 0}) == 0);
    CHECK(fixture.Call(195, {0x20000, 0x20200, 0, 0, 0, 0}) == -2);
}

TEST_CASE("Android getdents64 emits aligned records and pages") {
    MetadataSyscallFixture fixture;
    fixture.vfs.CreateDirectory("/sdcard");
    fixture.WriteString(0x20000, "/sdcard/dir");
    REQUIRE(fixture.Call(39, {0x20000, 0755, 0, 0, 0, 0}) == 0);
    for (const auto* name : {"/sdcard/dir/a.sav", "/sdcard/dir/b.sav"}) {
        fixture.WriteString(0x20100, name);
        const auto descriptor =
            fixture.Call(5, {0x20100, 0x40 | 1, 0, 0, 0, 0});
        REQUIRE(descriptor >= 3);
        REQUIRE(fixture.Call(6, {static_cast<std::uint32_t>(descriptor), 0, 0,
                                 0, 0, 0}) == 0);
    }

    // The guest obtains its directory descriptor through open(O_DIRECTORY),
    // not a test-only direct VFS call. The flag combination is exactly what
    // ARM bionic opendir() issues: O_RDONLY | O_DIRECTORY | O_CLOEXEC with
    // the ARM EABI value O_DIRECTORY = 040000 (arch/arm asm/fcntl.h).
    fixture.WriteString(0x20000, "/sdcard/dir");
    constexpr std::uint32_t kODirectory = 0x4000;
    constexpr std::uint32_t kOCloexec = 0x80000;
    const auto directory =
        fixture.Call(5, {0x20000, kODirectory | kOCloexec, 0, 0, 0, 0});
    REQUIRE(directory >= 3);

    // One 32-byte record fits. The second entry must remain pending for the
    // next call instead of being consumed and turned into -EINVAL.
    const auto written = fixture.Call(
        217, {static_cast<std::uint32_t>(directory), 0x20400, 32, 0, 0, 0});
    REQUIRE(written == 32);

    // linux_dirent64: u64 ino, s64 off, u16 reclen, u8 type, char name[].
    const auto reclen = fixture.ReadField(0x20400, 16, 2);
    CHECK(reclen % 8 == 0);
    CHECK(fixture.ReadField(0x20400, 18, 1) == 8);  // DT_REG
    std::vector<std::byte> name(6);  // "a.sav" plus its NUL
    fixture.memory.Read(GuestAddress{0x20400 + 19}, name);
    CHECK(static_cast<char>(name[0]) == 'a');
    CHECK(static_cast<char>(name[4]) == 'v');
    CHECK(static_cast<char>(name[5]) == '\0');
    CHECK(static_cast<std::uint64_t>(written) == reclen);

    const auto second = fixture.Call(
        217, {static_cast<std::uint32_t>(directory), 0x20400, 32, 0, 0, 0});
    CHECK(second == 32);
    std::vector<std::byte> second_name(6);
    fixture.memory.Read(GuestAddress{0x20400 + 19}, second_name);
    CHECK(static_cast<char>(second_name[0]) == 'b');
    CHECK(fixture.Call(217, {static_cast<std::uint32_t>(directory), 0x20400,
                             32, 0, 0, 0}) == 0);

    // fstat64 sees a directory and fsync(dirfd) is a safe metadata barrier.
    CHECK(fixture.Call(197, {static_cast<std::uint32_t>(directory), 0x20200,
                             0, 0, 0, 0}) == 0);
    CHECK((fixture.ReadField(0x20200, 16, 4) & 0170000U) == 0040000U);
    CHECK(fixture.Call(118, {static_cast<std::uint32_t>(directory), 0, 0, 0,
                             0, 0}) == 0);
    CHECK(fixture.Call(6, {static_cast<std::uint32_t>(directory), 0, 0, 0, 0,
                           0}) == 0);
}

TEST_CASE("Android fstat64 preserves the live file descriptor offset") {
    MetadataSyscallFixture fixture;
    fixture.vfs.CreateDirectory("/sdcard");
    fixture.WriteString(0x20000, "/sdcard/save.dat");
    const auto descriptor =
        fixture.Call(5, {0x20000, 0x40 | 2, 0, 0, 0, 0});
    REQUIRE(descriptor >= 3);
    const std::array payload{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    fixture.memory.Write(GuestAddress{0x20300}, payload);
    REQUIRE(fixture.Call(4, {static_cast<std::uint32_t>(descriptor), 0x20300,
                             3, 0, 0, 0}) == 3);
    REQUIRE(fixture.Call(19, {static_cast<std::uint32_t>(descriptor), 1, 0, 0,
                              0, 0}) == 1);

    CHECK(fixture.Call(197, {static_cast<std::uint32_t>(descriptor), 0x20200,
                             0, 0, 0, 0}) == 0);
    CHECK(fixture.ReadField(0x20200, 48, 8) == 3);
    CHECK(fixture.Call(3, {static_cast<std::uint32_t>(descriptor), 0x20320, 1,
                           0, 0, 0}) == 1);
    std::array<std::byte, 1> next{};
    fixture.memory.Read(GuestAddress{0x20320}, next);
    CHECK(next[0] == std::byte{'b'});
    CHECK(fixture.Call(6, {static_cast<std::uint32_t>(descriptor), 0, 0, 0, 0,
                           0}) == 0);
}

TEST_CASE("Android access, rename and positional IO keep their contracts") {
    MetadataSyscallFixture fixture;
    fixture.WriteString(0x20000, "/sdcard/old.sav");
    const auto descriptor = fixture.Call(5, {0x20000, 0x40 | 1, 0, 0, 0, 0});
    REQUIRE(descriptor >= 3);
    const std::array payload{std::byte{'0'}, std::byte{'1'}, std::byte{'2'},
                             std::byte{'3'}};
    fixture.memory.Write(GuestAddress{0x20300}, payload);
    REQUIRE(fixture.Call(4, {static_cast<std::uint32_t>(descriptor), 0x20300,
                             4, 0, 0, 0}) == 4);

    // pwrite64 must not disturb the descriptor offset.
    const std::array patch{std::byte{'X'}};
    fixture.memory.Write(GuestAddress{0x20310}, patch);
    CHECK(fixture.Call(181, {static_cast<std::uint32_t>(descriptor), 0x20310,
                             1, 0, 1, 0}) == 1);
    const std::array tail{std::byte{'4'}};
    fixture.memory.Write(GuestAddress{0x20320}, tail);
    CHECK(fixture.Call(4, {static_cast<std::uint32_t>(descriptor), 0x20320, 1,
                           0, 0, 0}) == 1);
    CHECK(fixture.Call(6, {static_cast<std::uint32_t>(descriptor), 0, 0, 0, 0,
                           0}) == 0);

    const auto reader = fixture.Call(5, {0x20000, 0, 0, 0, 0, 0});
    REQUIRE(reader >= 3);
    CHECK(fixture.Call(180, {static_cast<std::uint32_t>(reader), 0x20330, 5, 0,
                             0, 0}) == 5);
    std::vector<std::byte> read_back(5);
    fixture.memory.Read(GuestAddress{0x20330}, read_back);
    CHECK(static_cast<char>(read_back[1]) == 'X');
    CHECK(static_cast<char>(read_back[4]) == '4');
    CHECK(fixture.Call(6, {static_cast<std::uint32_t>(reader), 0, 0, 0, 0,
                           0}) == 0);

    // access: F_OK on a present file, ENOENT on an absent one.
    CHECK(fixture.Call(33, {0x20000, 0, 0, 0, 0, 0}) == 0);
    fixture.WriteString(0x20100, "/sdcard/new.sav");
    CHECK(fixture.Call(33, {0x20100, 0, 0, 0, 0, 0}) == -2);

    CHECK(fixture.Call(38, {0x20000, 0x20100, 0, 0, 0, 0}) == 0);
    CHECK(fixture.Call(33, {0x20100, 0, 0, 0, 0, 0}) == 0);
    CHECK(fixture.Call(33, {0x20000, 0, 0, 0, 0, 0}) == -2);
}

TEST_CASE("Android *at syscalls refuse relative paths instead of guessing") {
    MetadataSyscallFixture fixture;
    fixture.WriteString(0x20000, "relative/path");
    // No per-process cwd exists, so resolving these would pick a directory
    // at random; -ENOTSUP says so.
    CHECK(fixture.Call(323, {0xffffff9c, 0x20000, 0755, 0, 0, 0}) == -95);
    CHECK(fixture.Call(328, {0xffffff9c, 0x20000, 0, 0, 0, 0}) == -95);
    CHECK(fixture.Call(327, {0xffffff9c, 0x20000, 0x20200, 0, 0, 0}) == -95);
}
