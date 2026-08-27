// Monitor wait set across real host threads (DVM-29, design 04 §4).
// The sequence under test is AOSP vm/Sync.cpp waitMonitor: validate the
// owner, save the recursion depth, release the monitor completely, join the
// wait set, and only return (or throw InterruptedException) after the
// monitor has been re-acquired at the saved depth.

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/integration/dexvm_android.h"

namespace {

using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

std::vector<std::uint8_t> ReadFixture(const std::string& name) {
    const std::string path =
        std::string(OGPLAY_DEXVM_FIXTURE_DIR) + "/" + name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), "missing fixture: ", path);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
                                     std::istreambuf_iterator<char>());
}

struct MonitorVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;
    VmThreadRuntime threads;
    // Stands in for the lifecycle driver's published uptime: the tests move
    // it explicitly, so timed waits stay reproducible.
    std::atomic<std::int64_t> clock_millis{0};

    MonitorVm()
        : model(strings, arrays, {}),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, {}),
          threads(interpreter) {}

    void UseTestClock() {
        interpreter.Monitors().SetTimeSource(
            [this] { return clock_millis.load(); });
    }

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE_MESSAGE(java_class.has_value(), class_descriptor);
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE_MESSAGE(method.has_value(), name);
        return *method;
    }

    [[nodiscard]] VmCallOutcome CallStatic(const std::string& class_descriptor,
                                           const std::string& name,
                                           const std::string& descriptor) {
        return interpreter.Call(Static(class_descriptor, name, descriptor),
                                {});
    }

    [[nodiscard]] VmObjectRef Make(const std::string& class_descriptor) {
        const auto outcome =
            CallStatic(class_descriptor, "make", "()Ljava/lang/Runnable;");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.ref;
    }

    [[nodiscard]] VmObjectRef Lock() {
        const auto outcome =
            CallStatic("LWaitProbe;", "lock", "()Ljava/lang/Object;");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.ref;
    }

    [[nodiscard]] std::int32_t Observed() {
        const auto outcome = CallStatic("LWaitProbe;", "observed", "()I");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }

    [[nodiscard]] VmObjectRef NewThreadObject() {
        return interpreter.NewIntrinsicInstance("Ljava/lang/Object;");
    }
};

// Observes another host thread reaching a state. Every use below has a real
// terminating condition driven by this thread.
template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("dexvm notifyAll releases a waiter parked on another host thread") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto target = vm.Make("LWaitProbe;");
    const auto thread_object = target;

    vm.threads.Start(thread_object, "waiter", vm.threads.AllocateThreadId());
    // The waiter has to be in the wait set before anything signals it,
    // otherwise the test would pass without a wait set at all.
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));
    CHECK(std::ranges::any_of(
        vm.threads.Snapshot(), [thread_object](const auto& thread) {
            return thread.object == thread_object.Value() &&
                   thread.wait_state == VmThreadWaitState::monitor;
        }));
    CHECK(vm.Observed() == 0);

    // signal() takes the same monitor, so it only completes because wait()
    // released it entirely.
    const auto signalled = vm.CallStatic("LWaitProbe;", "signal", "()V");
    REQUIRE(!signalled.exception.IsValid());
    vm.threads.Join(thread_object);

    CHECK(vm.Observed() == 1);
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 0U);
    CHECK(!vm.threads.TakeFailure().has_value());
}

TEST_CASE("conditional swap pacing releases two swaps while driver waits") {
    MonitorVm vm;
    vm.UseTestClock();
    DexVmAndroidContext context;
    AttachEglSwapPacer(context, vm.interpreter.ExecutionLock());

    std::vector<std::int32_t> sequence;
    std::atomic<bool> record_failed{};
    std::atomic<bool> finished{};
    std::atomic<bool> watchdog_fired{};
    const auto record_swap =
        vm.Static("LSwapHandshake;", "recordSwap", "()V");
    std::thread worker([&] {
        for (std::int32_t swap = 1; swap <= 2; ++swap) {
            VmExecutionLockScope execution(vm.interpreter.ExecutionLock());
            PaceEglSwap(context, vm.interpreter.ExecutionLock());
            const auto recorded = vm.interpreter.Call(record_swap, {});
            if (recorded.exception.IsValid()) record_failed = true;
            sequence.push_back(swap);
        }
    });
    std::thread watchdog([&] {
        for (int attempt = 0; attempt < 5000 && !finished.load(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!finished.load()) {
            watchdog_fired = true;
            ShutdownEglSwapPacer(context);
        }
    });

    const auto awaited =
        vm.CallStatic("LSwapHandshake;", "awaitTwo", "()V");
    const bool await_failed = awaited.exception.IsValid();
    worker.join();
    finished = true;
    watchdog.join();
    CHECK_FALSE(watchdog_fired.load());
    CHECK_FALSE(await_failed);
    CHECK_FALSE(record_failed.load());
    const auto observed =
        vm.CallStatic("LSwapHandshake;", "observed", "()I");
    REQUIRE_FALSE(observed.exception.IsValid());
    CHECK(observed.value.AsInt() == 2);
    CHECK(sequence == std::vector<std::int32_t>{1, 2});

    DetachEglSwapPacer(context, vm.interpreter.ExecutionLock());
}

TEST_CASE("conditional swap pacing waits for frame when driver is runnable") {
    MonitorVm vm;
    DexVmAndroidContext context;
    AttachEglSwapPacer(context, vm.interpreter.ExecutionLock());
    std::atomic<bool> entered{};
    std::atomic<bool> completed{};

    std::thread worker([&] {
        VmExecutionLockScope execution(vm.interpreter.ExecutionLock());
        entered = true;
        PaceEglSwap(context, vm.interpreter.ExecutionLock());
        completed = true;
    });
    const bool did_enter = WaitFor([&] { return entered.load(); });
    CHECK(did_enter);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(completed.load());

    if (did_enter) {
        AdvanceEglSwapPacer(context);
    } else {
        ShutdownEglSwapPacer(context);
    }
    worker.join();
    CHECK(completed.load());
    DetachEglSwapPacer(context, vm.interpreter.ExecutionLock());
}

TEST_CASE("dexvm wait restores the full recursion depth before returning") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto target = vm.Make("LWaitRecursion;");
    const auto thread_object = target;

    vm.threads.Start(thread_object, "recursive-waiter",
                     vm.threads.AllocateThreadId());
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));
    // Depth three was released completely: this signal needs the monitor.
    REQUIRE(!vm.CallStatic("LWaitProbe;", "signal", "()V")
                 .exception.IsValid());
    vm.threads.Join(thread_object);

    // The body exits the monitor three times after waking. Without the
    // restored depth the second exit throws IllegalMonitorStateException,
    // which would land in TakeFailure instead of observed == 33.
    CHECK(!vm.threads.TakeFailure().has_value());
    CHECK(vm.Observed() == 33);
    CHECK(vm.interpreter.Monitors().HeldCount(1U) == 0U);
}

TEST_CASE("dexvm wait and notify require monitor ownership") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto notified =
        vm.CallStatic("LWaitProbe;", "notifyWithoutLock", "()V");
    REQUIRE(notified.exception.IsValid());
    CHECK(vm.linker.Class(notified.exception_class).descriptor ==
          "Ljava/lang/IllegalMonitorStateException;");

    const auto waited =
        vm.CallStatic("LWaitProbe;", "waitWithoutLock", "()V");
    REQUIRE(waited.exception.IsValid());
    CHECK(vm.linker.Class(waited.exception_class).descriptor ==
          "Ljava/lang/IllegalMonitorStateException;");
}

TEST_CASE("dexvm timed wait ends on the unified Clock deadline") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    // Run the timed wait on its own host thread so the test thread can move
    // the clock. Nothing ever notifies it: only the deadline gets it out.
    const auto waiter = vm.Make("LWaitTimedRunner;");
    const auto thread_object = waiter;
    vm.threads.Start(thread_object, "timed-waiter",
                     vm.threads.AllocateThreadId());
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));

    // Still parked well before the deadline.
    vm.clock_millis.store(49);
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 1U);

    vm.clock_millis.store(50);
    vm.threads.Join(thread_object);
    CHECK(!vm.threads.TakeFailure().has_value());
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 0U);
}

// AOSP vm/Sync.cpp waitMonitor rejects the (msec, nsec) pair as a unit
// before parking; the probes run without monitor ownership to pin that the
// argument check fires before the owner check, like wait(J)'s negative
// timeout in the monitor table.
TEST_CASE("dexvm Object.wait validates the millis/nanos pair") {
    MonitorVm vm;
    vm.UseTestClock();
    for (const auto* probe :
         {"waitNanoBadMillis", "waitNanoBadNanosLow", "waitNanoBadNanosHigh"}) {
        const auto outcome = vm.CallStatic("LWaitProbe;", probe, "()V");
        REQUIRE_MESSAGE(outcome.exception.IsValid(), probe);
        CHECK(vm.linker.Class(outcome.exception_class).descriptor ==
              "Ljava/lang/IllegalArgumentException;");
        CHECK(outcome.exception_message == "timeout arguments out of range");
        CHECK(vm.interpreter.Monitors().WaitingCount(vm.Lock()) == 0U);
    }
}

// A nonzero nanos remainder below one millisecond rounds the deadline up:
// wait(0, 999999) parks on a 1 ms deadline (it must not become the untimed
// wait(0, 0)), and only the deadline gets the waiter out.
TEST_CASE("dexvm Object.wait rounds a nonzero nanos onto the deadline") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto waiter = vm.Make("LWaitNanoRunner;");
    const auto thread_object = waiter;
    vm.threads.Start(thread_object, "nano-waiter",
                     vm.threads.AllocateThreadId());
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));

    // Clock still at 0: the rounded-up deadline has not arrived yet.
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 1U);

    vm.clock_millis.store(1);
    vm.threads.Join(thread_object);
    CHECK(!vm.threads.TakeFailure().has_value());
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 0U);
}

TEST_CASE("dexvm timed wait without a Clock fails instead of guessing") {
    MonitorVm vm;  // no time source published
    CHECK_THROWS_AS(static_cast<void>(vm.CallStatic("LWaitProbe;",
                                                    "waitTimedOnce", "()V")),
                    DexVmError);
    bool recorded = false;
    for (const auto& hit : vm.ledger.Unimplemented()) {
        if (hit.id == "dexvm.monitors.wait_without_clock") recorded = true;
    }
    CHECK(recorded);
}

TEST_CASE("dexvm interrupt wakes a waiter and re-acquires before throwing") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto target = vm.Make("LWaitInterrupt;");
    const auto thread_object = target;

    vm.threads.Start(thread_object, "interruptible",
                     vm.threads.AllocateThreadId());
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));

    vm.threads.Interrupt(thread_object);
    vm.threads.Join(thread_object);

    // The body caught InterruptedException and then exited the monitor. That
    // exit only balances if wait() re-acquired it before throwing.
    CHECK(!vm.threads.TakeFailure().has_value());
    CHECK(vm.Observed() == 7);
    // "The interrupted status of the current thread is cleared when this
    // exception is thrown" (JLS, AOSP waitMonitor done:).
    CHECK(!vm.threads.IsInterrupted(thread_object));
    CHECK(vm.interpreter.Monitors().WaitingCount(lock) == 0U);
}

TEST_CASE("dexvm interrupt before wait is honoured without parking") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    // Root context token is 1; interrupt it before it ever waits.
    vm.interpreter.Monitors().Interrupt(1U);
    vm.interpreter.Monitors().Enter(lock, 1U);
    const auto outcome = vm.interpreter.Monitors().Wait(lock, 1U, 0);
    CHECK(outcome == VmWaitOutcome::interrupted);
    // Ownership survives an interrupted wait.
    CHECK(vm.interpreter.Monitors().IsOwner(lock, 1U));
    CHECK(!vm.interpreter.Monitors().Interrupted(1U));
    vm.interpreter.Monitors().Exit(lock, 1U);
}

TEST_CASE("dexvm teardown wakes every waiter and stops the thread") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto target = vm.Make("LWaitProbe;");
    const auto thread_object = target;

    vm.threads.Start(thread_object, "waiter", vm.threads.AllocateThreadId());
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));

    vm.threads.Shutdown();
    CHECK(vm.threads.LiveCount() == 0U);
    CHECK(vm.interpreter.Monitors().ShuttingDown());
    // Waking at teardown is not a guest failure, and nothing was observed.
    CHECK(!vm.threads.TakeFailure().has_value());
}

TEST_CASE("dexvm monitor-exit without ownership is a guest exception") {
    MonitorVm vm;
    const auto lock = vm.Lock();
    CHECK_THROWS_AS(vm.interpreter.Monitors().Exit(lock, 1U), VmJavaThrow);
}

// The root lifecycle context parks on the very host thread that pumps a
// deterministic unified Clock (the frame loop), so a timed park on that
// Clock would wait forever. A pump-driven test clock plus the published
// advance models that session; without the fast-forward this wait parks
// forever, which is how a title licence poll in onCreate froze sessions.
TEST_CASE("dexvm root timed wait fast-forwards the published clock advance") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    std::vector<std::int64_t> advances;
    vm.interpreter.Monitors().SetClockAdvance(
        [&](const std::int64_t delta_millis) {
            vm.clock_millis += delta_millis;
            advances.push_back(delta_millis);
        });
    vm.interpreter.Monitors().Enter(lock, kRootLifecycleToken);
    CHECK(vm.interpreter.Monitors().Wait(lock, kRootLifecycleToken, 50) ==
          VmWaitOutcome::timed_out);
    CHECK(advances.size() == 1U);
    CHECK(advances.front() == 50);
    CHECK(vm.clock_millis.load() == 50);
    // The park restored the released monitor at the same depth.
    CHECK(vm.interpreter.Monitors().IsOwner(lock, kRootLifecycleToken));
    vm.interpreter.Monitors().Exit(lock, kRootLifecycleToken);
    CHECK(vm.interpreter.Monitors().HeldCount(kRootLifecycleToken) == 0U);
}
