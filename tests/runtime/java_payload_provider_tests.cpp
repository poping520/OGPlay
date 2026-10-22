#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

#include "ogplay/cpu/dynarmic.h"
#include "ogplay/hal/clock.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/java_payload_provider.h"
#include "ogplay/runtime/syscall/syscall.h"

namespace {
using namespace ogplay;
using namespace ogplay::runtime;
constexpr auto rw = memory::PageProtection::read | memory::PageProtection::write;

JavaPayloadLimits TinyLimits() {
    JavaPayloadLimits limits;
    limits.initial_heap_target_bytes = 64;
    limits.heap_growth_limit_bytes = 131072;
    limits.maximum_heap_bytes = 262144;
    limits.max_free_bytes = 128;
    limits.committed_limit_bytes = 131072;
    return limits;
}
}  // namespace

TEST_CASE("Java payload allocation packs aligned independent zero and small blocks") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider(space, ledger);
    auto empty = provider.Allocate(0);
    auto a = provider.Allocate(1);
    auto b = provider.Allocate(15);
    CHECK_FALSE(empty.Address().IsNull());
    CHECK(empty.Address() != a.Address());
    CHECK(a.Address() != b.Address());
    for (auto h : {empty, a, b})
        CHECK(h.Address().Value() % 8 == 0);
    auto stats = provider.Statistics();
    CHECK(stats.allocated_bytes == 16);
    CHECK(stats.allocation_count == 3);
    CHECK(stats.committed_bytes == 16384);
    CHECK(stats.slab_free_bytes == 16384 - 32);
    CHECK(provider.Access<std::uint8_t>(a).Read(0) == 0);
    provider.Access<std::uint8_t>(a).Write(0, 255);
    CHECK(provider.Access<std::uint8_t>(b).Read(0) == 0);
    CHECK_THROWS_AS(static_cast<void>(provider.Access<std::uint8_t>(empty).Read(0)), std::out_of_range);
    {
        auto active = provider.Access<std::uint8_t>(a);
        CHECK_THROWS_AS(provider.Release(a), std::invalid_argument);
        CHECK(active.Read(0) == 255);
    }
    auto address = a.Address();
    auto generation = a.Generation();
    provider.Release(a);
    CHECK_THROWS_AS(static_cast<void>(provider.Access<std::uint8_t>(a)), std::invalid_argument);
    auto replacement = provider.Allocate(1);
    CHECK(replacement.Address() == address);
    CHECK(replacement.Generation() != generation);
    CHECK(provider.Access<std::uint8_t>(replacement).Read(0) == 0);
    CHECK_THROWS_AS(static_cast<void>(provider.Release(a)), std::invalid_argument);
    provider.Release(empty);
    provider.Release(b);
    provider.Release(replacement);
    CHECK(provider.Statistics().committed_bytes == 0);
}

TEST_CASE("Java payload typed views enforce block and view boundaries and bit patterns") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider(space, ledger), other(space, ledger);
    auto block = provider.Allocate(16);
    auto neighbor = provider.Allocate(16);
    auto ints = provider.Access<std::int32_t>(block);
    ints.Write(3, -2147483647);
    CHECK(space.Read32(block.Address().Add(12)) == 0x80000001U);
    space.Write32(block.Address(), 0x7fc12345U);
    CHECK(std::bit_cast<std::uint32_t>(provider.Access<float>(block).Read(0)) == 0x7fc12345U);
    provider.Access<double>(block).Write(1, std::bit_cast<double>(UINT64_C(0xfff123456789abcd)));
    CHECK(space.Read64(block.Address().Add(8)) == UINT64_C(0xfff123456789abcd));
    CHECK_THROWS_AS(static_cast<void>(ints.Read(4)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(ints.Write(4, 7)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(provider.Access<std::uint64_t>(block, 8, 2)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(provider.Access<std::uint64_t>(block, 1, 1)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(provider.Access<std::uint8_t>(block, SIZE_MAX, 2)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(other.Access<std::int32_t>(block)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(other.Release(block)), std::invalid_argument);
    auto view = provider.Access<std::uint32_t>(block, 4, 1);
    CHECK_THROWS_AS(static_cast<void>(view.Write(1, 99)), std::out_of_range);
    CHECK(provider.Access<std::uint64_t>(neighbor).Read(0) == 0);
}

TEST_CASE("Java payload allocation threshold and release preserve neighboring guest pages") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    space.Map({memory::GuestAddress{0x10000}, space.PageSize()}, rw);
    space.Write32(memory::GuestAddress{0x10000}, 0xdeadbeef);
    GuestJavaPayloadProvider provider(space, ledger);
    auto a = provider.Allocate(8192);  // largest pooled allocation
    auto b = provider.Allocate(8193);  // standalone
    CHECK(a.Address().Value() >= 0x11000);
    CHECK(a.Address().Value() % space.BackingPageSize() == 0);
    CHECK(b.Address().Value() % space.BackingPageSize() == 0);
    auto c = provider.Allocate(8);
    provider.Access<std::uint64_t>(c).Write(0, UINT64_C(0xfedcba9876543210));
    provider.Release(b);
    CHECK(provider.Access<std::uint64_t>(c).Read(0) == UINT64_C(0xfedcba9876543210));
    CHECK(space.Read32(memory::GuestAddress{0x10000}) == 0xdeadbeef);
    provider.Release(a);
    provider.Release(c);
    CHECK(space.Read32(memory::GuestAddress{0x10000}) == 0xdeadbeef);
}

TEST_CASE("Java payload quota growth and failed allocations roll back all charges and mappings") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    auto limits = TinyLimits();
    limits.committed_limit_bytes = 16384;
    GuestJavaPayloadProvider provider(space, ledger, limits);
    auto a = provider.Allocate(80);
    const auto before = provider.Statistics();
    CHECK(before.heap_target_bytes == 208);
    const auto mappings = space.DescribeMappings().size();
    CHECK_THROWS_AS(static_cast<void>(provider.Allocate(100000)), std::bad_alloc);  // physical budget
    CHECK_THROWS_AS(static_cast<void>(provider.Allocate(131073)), std::bad_alloc);  // logical growth limit
    CHECK_THROWS_AS(static_cast<void>(provider.Allocate(SIZE_MAX)), std::bad_alloc);
    auto after = provider.Statistics();
    CHECK(after.allocated_bytes == before.allocated_bytes);
    CHECK(after.committed_bytes == before.committed_bytes);
    CHECK(after.slab_free_bytes == before.slab_free_bytes);
    CHECK(after.heap_target_bytes == before.heap_target_bytes);
    CHECK(after.allocation_count == before.allocation_count);
    CHECK(space.DescribeMappings().size() == mappings);
    auto b = provider.Allocate(8);  // allocator still usable after rollback
    provider.Release(b);
    provider.Release(a);
    CHECK_FALSE(ledger.Unimplemented().empty());
}

TEST_CASE("Java payload pin identities count unique bytes and prevent premature release") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider(space, ledger);
    auto block = provider.Allocate(32);
    auto first = provider.Pin(block), second = provider.Pin(block);
    CHECK(provider.Statistics().pin_count == 2);
    CHECK(provider.Statistics().pinned_bytes == 32);
    CHECK_THROWS_AS(static_cast<void>(provider.Release(block)), std::invalid_argument);
    provider.Unpin(first);
    CHECK(provider.Statistics().pinned_bytes == 32);
    CHECK_THROWS_AS(static_cast<void>(provider.Unpin(first)), std::invalid_argument);
    provider.Unpin(second);
    CHECK(provider.Statistics().pin_count == 0);
    CHECK(provider.Statistics().pinned_bytes == 0);
    provider.Release(block);
}

TEST_CASE("Java payload provider forbids restore even before its first allocation") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    auto snapshot = space.CaptureSnapshot();
    {
        GuestJavaPayloadProvider provider(space, ledger);
        CHECK_THROWS_AS(space.RestoreSnapshot(snapshot), memory::ProtectedMappingError);
        auto block = provider.Allocate(64);
        CHECK_THROWS_AS(space.RestoreSnapshot(snapshot), memory::ProtectedMappingError);
        provider.Release(block);
        CHECK_THROWS_AS(space.RestoreSnapshot(snapshot), memory::ProtectedMappingError);
    }
    CHECK_NOTHROW(space.RestoreSnapshot(snapshot));
    REQUIRE(ledger.Unimplemented().size() == 1);
    CHECK(ledger.Unimplemented()[0].id == "memory.java_payload.protected.restore");
    CHECK(ledger.Unimplemented()[0].count == 3);
}

TEST_CASE("Java payload protected mappings reject guest mutation including partial overlaps") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    auto dispatcher = CreateAndroidArmSyscallDispatcher(ledger);
    BindAndroidMemorySyscalls(dispatcher, space);
    const memory::GuestAddress previous{0x10000};
    space.Map({previous, 4096}, rw);
    space.Write32(previous, 91);
    GuestJavaPayloadProvider provider(space, ledger);
    auto block = provider.Allocate(64);
    const auto base = block.Address().Value();
    provider.Access<std::uint32_t>(block).Write(0, 42);
    const auto generation = space.TrySnapshot()->generation;
    for (auto number : {91U, 125U, 163U, 192U}) {
        for (bool overlap : {false, true}) {
            A32SyscallFrame frame;
            frame.number = number;
            frame.arguments = {overlap ? base - 4096 : base, overlap ? 8192U : 4096U, 4096, 0, 0, 0, 0};
            if (number == 125) frame.arguments[2] = 1;
            if (number == 192) {
                frame.arguments[2] = 3;
                frame.arguments[3] = 0x32;
            }
            CHECK(dispatcher.Dispatch(frame) == -1);  // EPERM policy, no partial change
        }
    }
    A32SyscallFrame target;
    target.number = 163;
    target.arguments = {previous.Value(), 4096, 4096, 3, base, 0, 0};
    CHECK(dispatcher.Dispatch(target) == -1);  // FIXED destination also protected
    CHECK(space.TrySnapshot()->generation == generation);
    CHECK(provider.Access<std::uint32_t>(block).Read(0) == 42);
    CHECK(space.Read32(previous) == 91);
    CHECK_FALSE(ledger.Unimplemented().empty());
    target.arguments[3] = 0;
    CHECK(dispatcher.Dispatch(target) == -kLinuxEnosys);  // general remap still unsupported
}

TEST_CASE("Java payload direct tables and dynarmic share the typed accessor backing") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    const memory::GuestAddress code{0x10000};
    space.Map({code, 4096}, rw);
    // ldr r1,[r0]; add r1,r1,#1; str r1,[r0]; svc #1
    const std::array<std::uint32_t, 4> instructions{0xe5901000U, 0xe2811001U, 0xe5801000U, 0xef000001U};
    for (std::size_t i = 0; i < instructions.size(); ++i)
        space.Write32(code.Add(i * 4), instructions[i]);
    space.Protect({code, 4096}, memory::PageProtection::read | memory::PageProtection::execute);
    GuestJavaPayloadProvider provider(space, ledger);
    auto block = provider.Allocate(16);
    auto view = provider.Access<std::uint32_t>(block);
    view.Write(0, 41);
    const auto page = block.Address().Value() >> memory::kGuestPageBits;
    REQUIRE((*space.DirectPageTable())[page] != nullptr);
    CHECK((*space.DirectReadPageTable())[page] == (*space.DirectPageTable())[page]);
    memory::CheckedMemoryBus bus(space);
    cpu::DynarmicCpu jit(bus);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    state.SetRegister(cpu::CoreRegister::r0, block.Address().Value());
    jit.SetState(state);
    REQUIRE(jit.Run(32).reason == cpu::RunStopReason::supervisor_call);
    CHECK(jit.GetState().Register(cpu::CoreRegister::r1) == 42);  // host write -> guest read
    CHECK(view.Read(0) == 42);                                    // guest write -> host read, no Read/Write bridge
    view.Write(0, 98);
    jit.SetState(state);
    REQUIRE(jit.Run(32).reason == cpu::RunStopReason::supervisor_call);
    CHECK(view.Read(0) == 99);
}

namespace {
constexpr std::size_t kBenchmarkCount = 1U << 16;
constexpr std::size_t kBenchmarkIterations = 10'000'000;
// Separate noinline kernels keep the two loops observable and equally bounded.
#if defined(_MSC_VER)
#define PAYLOAD_NOINLINE __declspec(noinline)
#else
#define PAYLOAD_NOINLINE __attribute__((noinline))
#endif
PAYLOAD_NOINLINE std::uint64_t VectorKernel(std::vector<std::uint32_t>& values) {
    std::uint64_t sum{};
    for (std::size_t i = 0; i < kBenchmarkIterations; ++i) {
        const auto index = i & (kBenchmarkCount - 1);
        const auto next = values.at((index + 1) & (kBenchmarkCount - 1)) + 1;
        values.at(index) = next;
        sum += next;
    }
    return sum;
}

PAYLOAD_NOINLINE std::uint64_t AccessorKernel(const JavaPayloadAccessor<std::uint32_t>& values) {
    std::uint64_t sum{};
    for (std::size_t i = 0; i < kBenchmarkIterations; ++i) {
        const auto index = i & (kBenchmarkCount - 1);
        const auto next = values.Read((index + 1) & (kBenchmarkCount - 1)) + 1;
        values.Write(index, next);
        sum += next;
    }
    return sum;
}

#undef PAYLOAD_NOINLINE
}  // namespace

TEST_CASE("Java payload Release microbenchmark vector versus typed accessor") {
    if (!std::getenv("OGPLAY_RUN_PAYLOAD_BENCHMARK")) return;
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    GuestJavaPayloadProvider provider(space, ledger);
    auto block = provider.Allocate(kBenchmarkCount * sizeof(std::uint32_t));
    auto accessor = provider.Access<std::uint32_t>(block);
    std::vector<std::uint32_t> values(kBenchmarkCount);
    std::vector<std::uint64_t> vector_ns, accessor_ns;
    for (unsigned round = 0; round < 7; ++round) {
        std::fill(values.begin(), values.end(), 0);
        for (std::size_t i = 0; i < kBenchmarkCount; ++i)
            accessor.Write(i, 0);
        std::uint64_t vector_sum{}, accessor_sum{};
        auto run_vector = [&] {
            const auto start = hal::Clock::SteadyTimestampNs();
            vector_sum = VectorKernel(values);
            vector_ns.push_back(hal::Clock::SteadyTimestampNs() - start);
        };
        auto run_accessor = [&] {
            const auto start = hal::Clock::SteadyTimestampNs();
            accessor_sum = AccessorKernel(accessor);
            accessor_ns.push_back(hal::Clock::SteadyTimestampNs() - start);
        };
        if (round % 2) {
            run_accessor();
            run_vector();
        } else {
            run_vector();
            run_accessor();
        }
        CHECK(vector_sum == accessor_sum);
    }
    std::sort(vector_ns.begin(), vector_ns.end());
    std::sort(accessor_ns.begin(), accessor_ns.end());
    const auto ratio = static_cast<double>(accessor_ns[3]) / static_cast<double>(vector_ns[3]);
    std::cout << "PAYLOAD_BENCHMARK iterations=" << kBenchmarkIterations << " bytes=" << block.Bytes()
              << " vector_median_ns=" << vector_ns[3] << " accessor_median_ns=" << accessor_ns[3] << " ratio=" << ratio
              << '\n';
}

TEST_CASE("Java payload typed accessor retains provider mapping until its last view retires") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    const auto snapshot = space.CaptureSnapshot();
    std::optional<JavaPayloadAccessor<std::uint32_t>> view;
    {
        GuestJavaPayloadProvider provider(space, ledger);
        auto block = provider.Allocate(8);
        view.emplace(provider.Access<std::uint32_t>(block));
        view->Write(0, 123);
        auto copied = *view;
        CHECK_THROWS_AS(provider.Release(block), std::invalid_argument);
        CHECK(copied.Read(0) == 123);
    }
    CHECK(view->Read(0) == 123);
    CHECK_THROWS_AS(space.RestoreSnapshot(snapshot), memory::ProtectedMappingError);
    view.reset();
    CHECK(space.DescribeMappings().empty());
    CHECK_NOTHROW(space.RestoreSnapshot(snapshot));
}

TEST_CASE("Java payload owner identity survives wrapper address reuse") {
    memory::AddressSpace space;
    core::CapabilityLedger ledger;
    std::optional<GuestJavaPayloadProvider> provider;
    provider.emplace(space, ledger);
    auto old = provider->Allocate(8);
    auto view = provider->Access<std::uint64_t>(old);
    view.Write(0, 456);
    provider.reset();
    provider.emplace(space, ledger);  // same wrapper address, different backing owner
    CHECK_THROWS_AS(static_cast<void>(provider->Access<std::uint64_t>(old)), std::invalid_argument);
    CHECK_THROWS_AS(provider->Release(old), std::invalid_argument);
    CHECK(view.Read(0) == 456);
}
