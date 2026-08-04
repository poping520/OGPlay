#include <doctest/doctest.h>

#include <cstdint>
#include <array>
#include <cstddef>
#include <atomic>
#include <thread>

#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/syscall.h"

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
    CHECK(coverage.declared >= 75);
    CHECK(coverage.implemented == 6);
    CHECK(coverage.declared_by_group.at(
              ogplay::runtime::SyscallGroup::file) > 20);
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
    CHECK(dispatcher.Dispatch(frame) == -1);

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
    CHECK(dispatcher.Dispatch(mismatch) == -11);

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
    CHECK(dispatcher.Dispatch(wake) == 1);
    waiter.join();
    CHECK(wait_result == 0);

    mismatch.arguments[0] = 0x10001;
    CHECK(dispatcher.Dispatch(mismatch) == -22);
    mismatch.arguments[0] = 0x20000;
    CHECK(dispatcher.Dispatch(mismatch) == -14);
    mismatch.arguments[0] = 0x10000;
    mismatch.arguments[3] = 0x10020;
    CHECK(dispatcher.Dispatch(mismatch) == -95);
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
    CHECK(dispatcher.Dispatch(frame) == 3);
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
    CHECK(dispatcher.Dispatch(frame) == 2);
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

TEST_CASE("ARM set_tls updates only the current guest thread pointer") {
    ogplay::core::CapabilityLedger ledger;
    auto dispatcher =
        ogplay::runtime::CreateAndroidArmSyscallDispatcher(ledger);
    ogplay::cpu::A32State state;
    state.SetThreadId(42);
    ogplay::runtime::BindAndroidArmPrivateSyscalls(
        dispatcher, [&state](const std::uint64_t thread_id,
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
    frame.thread_id = 43;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 0;
    CHECK(dispatcher.Dispatch(frame) == -3);
    frame.thread_id = 42;
    frame.arguments[0] = UINT32_MAX;
    CHECK(dispatcher.Dispatch(frame) == -22);
    CHECK_THROWS_AS(
        ogplay::runtime::BindAndroidArmPrivateSyscalls(dispatcher, {}),
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
    CHECK_THROWS_AS(ogplay::runtime::BindAndroidCloneSyscall(dispatcher, {}),
                    ogplay::runtime::SyscallError);
}
