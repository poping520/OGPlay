// Monitor wait set across real host threads (DVM-29, design 04 §4).
// The sequence under test is AOSP vm/Sync.cpp waitMonitor: validate the
// owner, save the recursion depth, release the monitor completely, join the
// wait set, and only return (or throw InterruptedException) after the
// monitor has been re-acquired at the saved depth.

#include <doctest/doctest.h>

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
    const auto thread_object = vm.NewThreadObject();

    vm.threads.Start(thread_object, target, "waiter");
    // The waiter has to be in the wait set before anything signals it,
    // otherwise the test would pass without a wait set at all.
    REQUIRE(WaitFor(
        [&] { return vm.interpreter.Monitors().WaitingCount(lock) == 1U; }));
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

TEST_CASE("dexvm wait restores the full recursion depth before returning") {
    MonitorVm vm;
    vm.UseTestClock();
    const auto lock = vm.Lock();
    const auto target = vm.Make("LWaitRecursion;");
    const auto thread_object = vm.NewThreadObject();

    vm.threads.Start(thread_object, target, "recursive-waiter");
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
    const auto thread_object = vm.NewThreadObject();
    vm.threads.Start(thread_object, waiter, "timed-waiter");
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
    const auto thread_object = vm.NewThreadObject();

    vm.threads.Start(thread_object, target, "interruptible");
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
    const auto thread_object = vm.NewThreadObject();

    vm.threads.Start(thread_object, target, "waiter");
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
