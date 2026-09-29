#include <doctest/doctest.h>
#include <array>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ogplay/cpu/execution_budget.h"
#include "ogplay/hal/clock.h"
#if OGPLAY_HAS_DYNARMIC
#include "ogplay/cpu/dynarmic.h"
#endif

namespace {
using namespace ogplay;
template<class Predicate> bool Await(Predicate predicate) {
    const auto end = hal::Clock::SteadyTimestampNs() + UINT64_C(2000000000);
    while (!predicate()) {
        if (hal::Clock::SteadyTimestampNs() >= end) return false;
        std::this_thread::yield();
    }
    return true;
}
}

TEST_CASE("BND-43 execution budget validates limits and refunds abandoned leases") {
    CHECK_THROWS_AS((cpu::ExecutionBudget{{33, 0}}), std::invalid_argument);
    CHECK_THROWS_AS((cpu::ExecutionBudget{{1, UINT64_C(10000000001)}}), std::invalid_argument);
    cpu::ExecutionBudget budget({1, 400}, [] { return UINT64_C(0); });
    std::atomic_bool canceled{false};
    { auto abandoned = budget.Acquire(10, canceled); CHECK(abandoned.Ticks() == 10); }
    CHECK(budget.Snapshot().active == 0);
    auto lease = budget.Acquire(10, canceled);
    CHECK_THROWS_AS(lease.Complete(11), std::logic_error);
    CHECK(budget.Snapshot().active == 0);
    CHECK(budget.Snapshot().consumed_ticks == 10);
    canceled = true;
    CHECK_FALSE(budget.Acquire(10, canceled));
    CHECK(budget.Snapshot().waiting == 0);
}

TEST_CASE("BND-43 process rate is aggregate and uses injected Clock credits") {
    std::atomic_uint64_t now{};
    cpu::ExecutionBudget budget({2, 400}, [&] { return now.load(); });
    std::atomic_bool canceled{false}, finished{false};
    auto first = budget.Acquire(10, canceled);
    first.Complete(6); // unused reservation must be immediately reusable
    auto second = budget.Acquire(4, canceled);
    second.Complete(4);
    auto remaining = budget.Acquire(10, canceled);
    remaining.Complete(10); // twenty initial ticks = 50 ms burst
    std::uint64_t admitted_at{};
    std::jthread worker([&] {
        auto next = budget.Acquire(10, canceled);
        if (next) { admitted_at = now.load(); next.Complete(10); }
        finished = true;
    });
    const bool parked = Await([&] { return budget.Snapshot().waiting == 1; });
    const bool early = finished.load();
    now = 25000000; // 25 ms * 400 ticks/s = ten new ticks
    budget.Notify();
    const bool woke = Await([&] { return finished.load(); });
    canceled = true; budget.Notify(); worker.join();
    CHECK(parked); CHECK_FALSE(early); CHECK(woke);
    CHECK(admitted_at == 25000000);
    CHECK(budget.Snapshot().consumed_ticks == 30);
    CHECK(budget.Snapshot().active == 0);
}

TEST_CASE("BND-43 admission is FIFO and process budgets are independent") {
    cpu::ExecutionBudget budget({1, 0});
    cpu::ExecutionBudget other({1, 0});
    std::atomic_bool canceled{false};
    auto held = budget.Acquire(1000000, canceled);
    CHECK(held.Ticks() == 50000);
    auto independent = other.Acquire(100, canceled);
    independent.Complete(100);
    std::vector<int> order; // only the one active lease writes
    std::jthread a([&] { auto lease = budget.Acquire(10, canceled); if (lease) { order.push_back(1); lease.Complete(10); } });
    const bool first_waiting = Await([&] { return budget.Snapshot().waiting == 1; });
    std::jthread b([&] { auto lease = budget.Acquire(10, canceled); if (lease) { order.push_back(2); lease.Complete(10); } });
    const bool second_waiting = Await([&] { return budget.Snapshot().waiting == 2; });
    held.Complete(50);
    a.join(); b.join();
    CHECK(first_waiting); CHECK(second_waiting);
    CHECK(order == std::vector<int>{1, 2});
    CHECK(budget.Snapshot().peak_active == 1);
    CHECK(budget.Snapshot().consumed_ticks == 70);
    CHECK(other.Snapshot().consumed_ticks == 100);
}

TEST_CASE("BND-43 cancellation and teardown release admission and rate waits") {
    for (const bool parallel : {false, true}) {
    for (const bool drain : {false, true}) {
        cpu::ExecutionBudget budget({parallel ? 1U : 0U, 400}, [] { return UINT64_C(0); });
        std::atomic_bool canceled{false}, finished{false}, admitted{false};
        auto held = budget.Acquire(10, canceled);
        if (!parallel) {
            held.Complete(10);
            auto remaining = budget.Acquire(10, canceled);
            remaining.Complete(10);
        }
        std::jthread worker([&] {
            auto lease = budget.Acquire(10, canceled);
            admitted = static_cast<bool>(lease);
            if (lease) lease.Complete(3);
            finished = true;
        });
        const bool parked = Await([&] { return budget.Snapshot().waiting == 1; });
        if (drain) budget.BeginDrain();
        else { canceled = true; budget.Notify(); }
        const bool woke = Await([&] { return finished.load(); });
        if (held) held.Complete(10);
        canceled = true; budget.Notify(); worker.join();
        CHECK(parked); CHECK(woke); CHECK(admitted.load() == drain);
        CHECK(budget.Snapshot().waiting == 0);
        CHECK(budget.Snapshot().active == 0);
        CHECK(budget.Snapshot().draining == drain);
    }
    }
}

#if OGPLAY_HAS_DYNARMIC
namespace {
class HoldFetch final : public memory::MemoryAccessObserver {
public:
    void OnMemoryAccess(const memory::BusAccess& access) override {
        if (access.type != memory::BusAccessType::execute) return;
        entered.fetch_or(1U << static_cast<unsigned>(access.thread_id - 1));
        released.wait(false);
    }
    void Release() { released = true; released.notify_all(); }
    std::atomic_uint entered{};
    std::atomic_bool released{};
};
}

TEST_CASE("BND-43 shared Dynarmic context enforces actual concurrent Run admission") {
    for (const auto limit : {1U, 2U}) {
        memory::AddressSpace memory;
        HoldFetch observer;
        memory::CheckedMemoryBus bus(memory, &observer);
        constexpr memory::GuestAddress code{0x10000};
        memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
        memory.Write32(code, 0xe3a0002a); // mov r0, #42
        memory.Write32(code.Add(4), 0xef00004d); // svc #77
        memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
        auto budget = std::make_shared<cpu::ExecutionBudget>(cpu::ExecutionBudgetConfig{limit, 0});
        auto context = std::make_shared<cpu::DynarmicExecutionContext>(2, budget);
        cpu::DynarmicCpu first(bus, context), second(bus, context);
        cpu::A32State state;
        state.SetRegister(cpu::CoreRegister::pc, code.Value());
        state.SetThreadId(1); first.SetState(state);
        state.SetThreadId(2); second.SetState(state);
        std::atomic_uint fast_calls{};
        const cpu::HostCallHook hook{+[](void* opaque, std::uint32_t, cpu::A32HostCallContext&) noexcept {
            ++*static_cast<std::atomic_uint*>(opaque);
            return cpu::HostCallResult::handled;
        }, &fast_calls};
        first.SetHostCallHook(hook); second.SetHostCallHook(hook);
        std::array<cpu::RunResult, 2> results;
        std::array<std::exception_ptr, 2> failures;
        std::jthread a([&] { try { results[0] = first.Run(100); } catch (...) { failures[0] = std::current_exception(); } });
        const bool first_entered = Await([&] { return observer.entered.load() != 0; });
        std::jthread b([&] { try { results[1] = second.Run(100); } catch (...) { failures[1] = std::current_exception(); } });
        const bool reached = Await([&] {
            return limit == 1 ? budget->Snapshot().waiting == 1 : observer.entered.load() == 3;
        });
        const auto active = budget->Snapshot().active;
        observer.Release(); a.join(); b.join();
        CHECK(first_entered); CHECK(reached); CHECK(active == limit);
        CHECK_FALSE(failures[0]); CHECK_FALSE(failures[1]);
        CHECK(budget->Snapshot().peak_active == limit);
        CHECK(budget->Snapshot().active == 0); // HLE/JNI dispatch can now block/reenter
        CHECK(fast_calls.load() == 0);
        for (const auto& result : results) {
            CHECK(result.reason == cpu::RunStopReason::supervisor_call);
            CHECK(result.immediate == 77);
        }
        CHECK(budget->Snapshot().consumed_ticks == results[0].ticks_consumed + results[1].ticks_consumed);
    }
}

TEST_CASE("BND-43 Dynarmic RequestHalt wakes a CPU queued behind another thread") {
    memory::AddressSpace memory;
    HoldFetch observer;
    memory::CheckedMemoryBus bus(memory, &observer);
    constexpr memory::GuestAddress code{0x10000};
    memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
    memory.Write32(code, 0xef00004d);
    memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
    auto budget = std::make_shared<cpu::ExecutionBudget>(cpu::ExecutionBudgetConfig{1, 0});
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(2, budget);
    cpu::DynarmicCpu first(bus, context), second(bus, context);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    state.SetThreadId(1); first.SetState(state);
    state.SetThreadId(2); second.SetState(state);
    std::atomic_bool stopped{false};
    cpu::RunResult result;
    std::jthread a([&] { static_cast<void>(first.Run(10)); });
    const bool entered = Await([&] { return observer.entered.load() != 0; });
    std::jthread b([&] { result = second.Run(10); stopped = true; });
    const bool waiting = Await([&] { return budget->Snapshot().waiting == 1; });
    second.RequestHalt();
    const bool canceled = Await([&] { return stopped.load(); });
    observer.Release(); a.join(); b.join();
    CHECK(entered); CHECK(waiting); CHECK(canceled);
    CHECK(result.reason == cpu::RunStopReason::halt_requested);
    CHECK(result.ticks_consumed == 0);
    CHECK(budget->Snapshot().waiting == 0);
    CHECK(budget->Snapshot().active == 0);
}

TEST_CASE("BND-43 Dynarmic CPUs consume one shared rate bucket across Run slices") {
    memory::AddressSpace memory;
    memory::CheckedMemoryBus bus(memory);
    constexpr memory::GuestAddress code{0x10000};
    memory.Map({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::write);
    memory.Write32(code, 0xe2800001); // add r0, r0, #1
    memory.Write32(code.Add(4), 0xeafffffd); // b loop
    memory.Protect({code, memory.PageSize()}, memory::PageProtection::read | memory::PageProtection::execute);
    std::atomic_uint64_t now{};
    auto budget = std::make_shared<cpu::ExecutionBudget>(cpu::ExecutionBudgetConfig{2, 400}, [&] { return now.load(); });
    auto context = std::make_shared<cpu::DynarmicExecutionContext>(2, budget);
    cpu::DynarmicCpu first(bus, context), second(bus, context);
    cpu::A32State state;
    state.SetRegister(cpu::CoreRegister::pc, code.Value());
    state.SetThreadId(1); first.SetState(state);
    state.SetThreadId(2); second.SetState(state);
    const auto one = first.Run(1000);
    CHECK(one.reason == cpu::RunStopReason::budget_exhausted);
    CHECK(one.ticks_consumed == 10);
    CHECK(first.Run(1000).ticks_consumed == 10); // consume remainder of initial burst
    std::atomic_bool finished{false};
    cpu::RunResult two;
    std::jthread worker([&] { two = second.Run(1000); finished = true; });
    const bool waiting = Await([&] { return budget->Snapshot().waiting == 1; });
    const bool early = finished.load();
    now = 25000000; budget->Notify();
    const bool woke = Await([&] { return finished.load(); });
    if (!woke) second.RequestHalt();
    worker.join();
    CHECK(waiting); CHECK_FALSE(early); CHECK(woke);
    CHECK(two.reason == cpu::RunStopReason::budget_exhausted);
    CHECK(two.ticks_consumed == 10);
    CHECK(budget->Snapshot().consumed_ticks == 30);
}
#endif
