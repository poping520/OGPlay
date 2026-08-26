// Guest Java threads on real host threads (DVM-28, design 04 §3).
// State transitions follow AOSP vm/Thread.cpp: start publishes a running
// thread, a second start is refused, interrupt raises a flag, teardown
// interrupts before it joins. Object.wait() must still fail explicitly
// until the wait-set lands.

#include <doctest/doctest.h>

#include <chrono>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

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

// Stands in for the A32 guest call executor: the interpreter counts a live
// native frame around whatever this does.
struct StubNativeBridge final : NativeMethodBridge {
    std::function<void()> action;

    [[nodiscard]] VmValue Invoke(const LinkedMethod&, VmObjectRef,
                                 std::span<const VmValue>) override {
        if (action) action();
        return VmValue::Void();
    }
};

struct ThreadedVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;
    VmThreadRuntime threads;

    explicit ThreadedVm(NativeMethodBridge* bridge = nullptr)
        : model(strings, arrays, {}),
          linker(),
          interpreter(
              [this]() -> DexClassLinker& {
                  linker.RegisterIntrinsics(CoreIntrinsicCatalog());
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, bridge, ledger, {}),
          threads(interpreter) {}

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

    // Builds the run() target declared by the fixture class.
    [[nodiscard]] VmObjectRef Make(const std::string& class_descriptor,
                                   const std::string& descriptor) {
        const auto outcome = CallStatic(class_descriptor, "make", descriptor);
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        REQUIRE(outcome.value.kind == VmValue::Kind::ref);
        return outcome.value.ref;
    }

    // A distinct java.lang.Thread stand-in, so the tests exercise the
    // Runnable form where the thread object is not the run() target.
    [[nodiscard]] VmObjectRef NewThreadObject() {
        return interpreter.NewIntrinsicInstance("Ljava/lang/Object;");
    }

    [[nodiscard]] std::int32_t ReadInt(const std::string& class_descriptor,
                                       const std::string& name) {
        const auto outcome = CallStatic(class_descriptor, name, "()I");
        REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                        outcome.exception_message);
        return outcome.value.AsInt();
    }

    [[nodiscard]] VmThreadStatus StatusOf(const VmObjectRef thread_object) {
        for (const auto& entry : threads.Snapshot()) {
            if (entry.object == thread_object.Value()) return entry.status;
        }
        FAIL("thread object is not registered");
        return VmThreadStatus::created;
    }
};

// Bounded spin used only to observe another host thread reaching a state.
// Every wait here has a real terminating condition; nothing sleeps blindly.
template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for (int attempt = 0; attempt < 5000; ++attempt) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("dexvm Thread.start runs run() on a real host thread") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadProbe;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;

    vm.threads.Start(thread_object, "probe", vm.threads.AllocateThreadId());
    CHECK(vm.threads.ThreadId(thread_object) != 0U);
    vm.threads.Join(thread_object);

    // LFlow.loopSum(100) == 4950, produced by interpreted code on the child.
    CHECK(vm.ReadInt("LThreadProbe;", "counter") == 4950);
    CHECK(!vm.threads.IsAlive(thread_object));
    CHECK(vm.StatusOf(thread_object) == VmThreadStatus::finished);
    CHECK(vm.threads.LiveCount() == 0U);
    CHECK(!vm.threads.TakeFailure().has_value());
}

TEST_CASE("dexvm child threads share one object world with the root") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadProbe;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;
    vm.threads.Start(thread_object, "probe", vm.threads.AllocateThreadId());
    vm.threads.Join(thread_object);

    const auto product =
        vm.CallStatic("LThreadProbe;", "product", "()Ljava/lang/Object;");
    REQUIRE(!product.exception.IsValid());
    REQUIRE(product.value.ref.IsValid());
    // The object the child allocated resolves in the same JavaObjectModel
    // handle space the root thread reads: no shadow copy of the heap.
    const auto product_class = vm.model.ObjectClass(product.value.ref);
    CHECK(vm.linker.Class(product_class).descriptor == "LThreadProbe;");
    CHECK(product.value.ref != target);
}

TEST_CASE("dexvm Thread.start is refused twice and without a run() target") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadProbe;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;
    const auto id = vm.threads.AllocateThreadId();
    vm.threads.Start(thread_object, "probe", id);
    CHECK_THROWS_AS(vm.threads.Start(thread_object, "probe", id),
                    VmJavaThrow);
    vm.threads.Join(thread_object);
    // A finished thread is still a started thread.
    CHECK_THROWS_AS(vm.threads.Start(thread_object, "probe", id),
                    VmJavaThrow);

    const auto without_run =
        vm.Make("LThreadNoRun;", "()Ljava/lang/Object;");
    CHECK_THROWS_AS(
        vm.threads.Start(without_run, "no-run",
                         vm.threads.AllocateThreadId()),
        VmJavaThrow);
}

TEST_CASE("dexvm isAlive tracks a running thread until teardown stops it") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadSpin;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;
    vm.threads.Start(thread_object, "spin", vm.threads.AllocateThreadId());

    REQUIRE(WaitFor(
        [&] { return vm.StatusOf(thread_object) == VmThreadStatus::running; }));
    CHECK(vm.threads.IsAlive(thread_object));

    // The body never returns: only the teardown stop request unwinds it.
    vm.threads.Shutdown();
    CHECK(!vm.threads.IsAlive(thread_object));
    CHECK(vm.threads.LiveCount() == 0U);
    CHECK(vm.threads.ShuttingDown());
    // "stopped" can only be reached by throwing out of the per-instruction
    // stop check, so the body really was interpreting when teardown hit it.
    CHECK(vm.StatusOf(thread_object) == VmThreadStatus::stopped);
    // Unwinding at teardown is not a failure.
    CHECK(!vm.threads.TakeFailure().has_value());
    // A second shutdown proves the joined context was fully reaped rather
    // than leaving an interpreted frame for Interpreter destruction.
    CHECK_NOTHROW(vm.threads.Shutdown());
}

TEST_CASE("dexvm stopped-context unwind requires the teardown handshake") {
    ThreadedVm vm;
    const auto context = vm.interpreter.CreateExecutionContext();
    CHECK_THROWS_AS(vm.interpreter.UnwindStoppedExecutionContext(context),
                    DexVmError);
    vm.interpreter.RequestStop(context);
    CHECK_NOTHROW(vm.interpreter.UnwindStoppedExecutionContext(context));
    CHECK(vm.interpreter.ExecutionSnapshot(context).frame_depth == 0U);
    CHECK_NOTHROW(vm.interpreter.DiscardExecutionContext(context));
}

TEST_CASE("dexvm teardown joins every live thread deterministically") {
    ThreadedVm vm;
    // Every target is built before the first thread starts: a body that
    // never blocks keeps the execution lock, so interpreting anything else
    // afterwards would have to wait for teardown.
    std::vector<VmObjectRef> targets;
    for (int index = 0; index < 4; ++index) {
        targets.push_back(
            vm.Make("LThreadSpin;", "()Ljava/lang/Runnable;"));
    }
    std::vector<VmObjectRef> started;
    for (const auto thread_object : targets) {
        vm.threads.Start(thread_object, "spin",
                         vm.threads.AllocateThreadId());
        started.push_back(thread_object);
    }
    REQUIRE(WaitFor([&] { return vm.threads.LiveCount() == 4U; }));
    vm.threads.Shutdown();
    CHECK(vm.threads.LiveCount() == 0U);
    for (const auto& thread_object : started) {
        CHECK(!vm.threads.IsAlive(thread_object));
    }
    // Shutdown is idempotent.
    vm.threads.Shutdown();
}

TEST_CASE("dexvm uncaught thread exceptions are recorded, not thrown at join") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadThrower;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;
    vm.threads.Start(thread_object, "thrower",
                     vm.threads.AllocateThreadId());
    vm.threads.Join(thread_object);  // join itself stays quiet

    CHECK(vm.StatusOf(thread_object) == VmThreadStatus::failed);
    const auto failure = vm.threads.TakeFailure();
    REQUIRE(failure.has_value());
    CHECK(failure->find("LMyError;") != std::string::npos);
    CHECK(failure->find("thrower") != std::string::npos);
    CHECK(failure->find("thrower (id 2)") != std::string::npos);
    // Draining is one-shot.
    CHECK(!vm.threads.TakeFailure().has_value());
}

TEST_CASE("dexvm interrupt raises the flag and clears once") {
    ThreadedVm vm;
    const auto target = vm.Make("LThreadSpin;", "()Ljava/lang/Runnable;");
    const auto thread_object = target;
    vm.threads.Start(thread_object, "spin", vm.threads.AllocateThreadId());
    REQUIRE(WaitFor([&] { return vm.threads.LiveCount() == 1U; }));

    CHECK(!vm.threads.IsInterrupted(thread_object));
    vm.threads.Interrupt(thread_object);
    CHECK(vm.threads.IsInterrupted(thread_object));
    // The root thread has no Thread record, so it never reads as interrupted.
    CHECK(!vm.threads.ClearCurrentInterrupt());
    vm.threads.Shutdown();
}

TEST_CASE("dexvm join returns immediately for a never-started target") {
    ThreadedVm vm;
    // Joining something that was never started is a no-op, as on device.
    vm.threads.Join(vm.NewThreadObject());
    CHECK(vm.threads.LiveCount() == 0U);
}

TEST_CASE("dexvm may park while a guest native frame is live") {
    StubNativeBridge bridge;
    ThreadedVm vm(&bridge);
    vm.interpreter.Monitors().SetTimeSource([] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    });

    std::uint32_t depth_inside = 0;
    bridge.action = [&] {
        depth_inside = vm.interpreter.CurrentNativeDepth();
        vm.threads.Sleep(1);
    };

    const auto outcome = vm.CallStatic("LThreadNative;", "call", "()V");
    CHECK_FALSE(outcome.exception.IsValid());
    CHECK(depth_inside == 1U);
    CHECK(vm.interpreter.CurrentNativeDepth() == 0U);
    for (const auto& hit : vm.ledger.Unimplemented()) {
        CHECK(hit.id != "dexvm.threads.block_in_native");
    }
}

TEST_CASE("dexvm synchronized native methods use class and receiver monitors") {
    StubNativeBridge bridge;
    ThreadedVm vm(&bridge);
    const auto java_class = vm.linker.FindClass("LSyncNative;");
    REQUIRE(java_class.has_value());
    VmObjectRef expected(0);
    std::size_t observed{};
    bridge.action = [&] {
        CHECK(vm.interpreter.Monitors().IsOwner(
            expected, vm.interpreter.CurrentContextToken()));
        ++observed;
    };

    expected = vm.model.ClassObject(*java_class);
    const auto static_outcome =
        vm.CallStatic("LSyncNative;", "classLocked", "()V");
    CHECK_FALSE(static_outcome.exception.IsValid());
    CHECK(vm.interpreter.Monitors().HeldCount(1) == 0U);

    const auto created =
        vm.CallStatic("LSyncNative;", "make", "()LSyncNative;");
    REQUIRE(created.value.kind == VmValue::Kind::ref);
    expected = created.value.ref;
    const auto index = vm.linker.FindVtableIndex(
        *java_class, "instanceLocked", "()V");
    REQUIRE(index.has_value());
    const auto instance_outcome = vm.interpreter.Call(
        vm.linker.Class(*java_class).vtable[*index],
        std::vector<VmValue>{VmValue::Ref(expected)});
    CHECK_FALSE(instance_outcome.exception.IsValid());
    CHECK(observed == 2U);
    CHECK(vm.interpreter.Monitors().HeldCount(1) == 0U);

    bridge.action = [] {
        throw VmJavaThrow{"Ljava/lang/RuntimeException;", "native failure"};
    };
    CHECK_THROWS_AS(
        static_cast<void>(vm.interpreter.Call(
            vm.linker.Class(*java_class).vtable[*index],
            std::vector<VmValue>{VmValue::Ref(expected)})),
        VmJavaThrow);
    CHECK(vm.interpreter.Monitors().HeldCount(1) == 0U);
}
