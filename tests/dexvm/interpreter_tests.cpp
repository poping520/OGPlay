#include "boot_dex.h"
// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/text.h"
#include "ogplay/hal/clock.h"
#include "ogplay/runtime/dexvm/access_flags.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

#include "interpreter_fixture.h"

using namespace ogplay::test::interpreter;
using namespace ogplay::runtime;
using namespace ogplay::runtime::dexvm;

TEST_CASE("dexvm arithmetic edge semantics") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    // OP_DIV_INT.cpp: MIN_INT / -1 == MIN_INT (no trap).
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(7), VmValue::Int(-2)}),
              -3);
    // OP_REM_INT.cpp: MIN_INT % -1 == 0.
    ExpectInt(vm.CallStatic("LArith;", "remainder", "(II)I",
                            {VmValue::Int(std::numeric_limits<
                                          std::int32_t>::min()),
                             VmValue::Int(-1)}),
              0);
    // OP_DIV_INT.cpp: divide by zero throws ArithmeticException.
    const auto division = vm.CallStatic(
        "LArith;", "divide", "(II)I", {VmValue::Int(1), VmValue::Int(0)});
    ExpectException(vm, division, "Ljava/lang/ArithmeticException;");
    // In-method catch handler observes the exception.
    ExpectInt(vm.CallStatic("LArith;", "divideCaught", "(II)I",
                            {VmValue::Int(1), VmValue::Int(0)}),
              -99);
    // OP_CMPL_FLOAT.cpp: NaN biases to -1; OP_CMPG_FLOAT.cpp biases to +1.
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              -1);
    ExpectInt(vm.CallStatic("LArith;", "cmpgFloat", "(FF)I",
                            {VmValue::Float(nan), VmValue::Float(0.0f)}),
              1);
    ExpectInt(vm.CallStatic("LArith;", "cmplFloat", "(FF)I",
                            {VmValue::Float(2.0f), VmValue::Float(1.0f)}),
              1);
    // OP_SHL_INT.cpp: shift distance masks to 5 bits (33 -> 1).
    ExpectInt(vm.CallStatic("LArith;", "shifts", "(II)I",
                            {VmValue::Int(3), VmValue::Int(33)}),
              6);
    // OP_USHR_INT.cpp: logical shift of negative operand.
    ExpectInt(vm.CallStatic("LArith;", "ushr", "(II)I",
                            {VmValue::Int(-1), VmValue::Int(28)}),
              15);
    // OP_FLOAT_TO_INT.cpp: NaN -> 0, +inf -> MAX_INT.
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(nan)}),
              0);
    ExpectInt(vm.CallStatic("LArith;", "floatToInt", "(F)I",
                            {VmValue::Float(
                                std::numeric_limits<float>::infinity())}),
              std::numeric_limits<std::int32_t>::max());
    // OP_INT_TO_SHORT.cpp: truncation with sign extension.
    ExpectInt(vm.CallStatic("LArith;", "intToShort", "(I)I",
                            {VmValue::Int(0x18000)}),
              static_cast<std::int32_t>(
                  static_cast<std::int16_t>(0x8000)));
    // OP_MUL_LONG.cpp: 64-bit wraparound multiply.
    const auto product = vm.CallStatic(
        "LArith;", "longMul", "(JJ)J",
        {VmValue::Long(0x100000001LL), VmValue::Long(3)});
    REQUIRE(product.value.kind == VmValue::Kind::wide);
    CHECK(product.value.AsLong() == 0x300000003LL);
    });
}

TEST_CASE("dexvm control flow and switches") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    ExpectInt(vm.CallStatic("LFlow;", "loopSum", "(I)I", {VmValue::Int(10)}),
              45);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(1)}), 1);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(2)}), 22);
    ExpectInt(vm.CallStatic("LFlow;", "pick", "(I)I", {VmValue::Int(9)}), 0);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(-5)}),
              111);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I",
                            {VmValue::Int(1000)}),
              222);
    ExpectInt(vm.CallStatic("LFlow;", "sparse", "(I)I", {VmValue::Int(3)}),
              -1);
    });
}

TEST_CASE("dexvm arrays and implicit exceptions") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    ExpectInt(vm.CallStatic("LFlow;", "sumArray", "()I"), 24);
    ExpectException(vm, vm.CallStatic("LFlow;", "outOfBounds", "()I"),
                    "Ljava/lang/ArrayIndexOutOfBoundsException;");
    ExpectException(vm, vm.CallStatic("LFlow;", "npe", "()I"),
                    "Ljava/lang/NullPointerException;");
    });
}

TEST_CASE("dexvm objects, fields and dispatch") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    // Instance fields via <init>/iget/iput and virtual dispatch.
    ExpectInt(vm.CallStatic("LTask;", "exercise", "()I"), 77);
    ExpectInt(vm.CallStatic("LTypes;", "isRunnable", "()I"), 1);
    ExpectException(vm, vm.CallStatic("LTypes;", "badCast", "()V"),
                    "Ljava/lang/ClassCastException;");
    ExpectInt(vm.CallStatic("LTypes;", "strings", "()I"), 6);
    });
}

TEST_CASE("dexvm Object.clone is a shallow copy gated by Cloneable") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    // Cloneable check and payload copy follow libcore Object.java plus
    // AOSP vm/alloc/Alloc.cpp dvmCloneObject at the pinned baseline.
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneFields", "()I"), 1);
    const auto denied = vm.CallStatic("LCloneProbe;", "clonePlainObject",
                                      "()Ljava/lang/Object;");
    ExpectException(vm, denied, "Ljava/lang/CloneNotSupportedException;");
    CHECK(denied.exception_message == "Class doesn't implement Cloneable");
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneInts", "()I"), 1);
    ExpectInt(vm.CallStatic("LCloneProbe;", "cloneObjects", "()I"), 1);
    const auto ints = vm.linker.ResolveDescriptor("[I");
    const auto cloneable =
        vm.linker.ResolveDescriptor("Ljava/lang/Cloneable;");
    const auto serializable =
        vm.linker.ResolveDescriptor("Ljava/io/Serializable;");
    CHECK(vm.linker.IsAssignable(cloneable, ints));
    CHECK(vm.linker.IsAssignable(serializable, ints));
    // DVM-110: interface components are references even without a superclass.
    const auto object = vm.linker.ResolveDescriptor("Ljava/lang/Object;");
    const auto runnable = vm.linker.ResolveDescriptor("Ljava/lang/Runnable;");
    const auto objects = vm.linker.ResolveDescriptor("[Ljava/lang/Object;");
    const auto runnables = vm.linker.ResolveDescriptor("[Ljava/lang/Runnable;");
    CHECK(vm.linker.IsAssignable(object, runnable));
    CHECK(vm.linker.IsAssignable(objects, runnables));
    CHECK_FALSE(vm.linker.IsAssignable(runnables, objects));
    CHECK_FALSE(vm.linker.IsAssignable(objects, ints));
    CHECK_FALSE(vm.linker.IsAssignable(object, vm.linker.ResolveDescriptor("I")));
    });
}

TEST_CASE("dexvm virtual and super dispatch through subclass") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    // Construct LDoubler and call describe() virtually via LCounter ref.
    const auto doubler_class = vm.linker.FindClass("LDoubler;");
    REQUIRE(doubler_class.has_value());
    vm.linker.EnsureClassLinked(*doubler_class);
    const auto init =
        vm.linker.FindDirectMethod(*doubler_class, "<init>", "(I)V");
    REQUIRE(init.has_value());
    const auto instance = vm.model.NewInstance(
        *doubler_class, vm.linker.Class(*doubler_class).instance_slots);
    const auto construct = vm.interpreter.Call(
        *init, std::vector<VmValue>{VmValue::Ref(instance), VmValue::Int(4)});
    REQUIRE(!construct.exception.IsValid());

    const auto index = vm.linker.FindVtableIndex(*doubler_class, "describe",
                                                 "()I");
    REQUIRE(index.has_value());
    const auto target = vm.linker.Class(*doubler_class).vtable[*index];
    const auto outcome = vm.interpreter.Call(
        target, std::vector<VmValue>{VmValue::Ref(instance)});
    ExpectInt(outcome, 200);  // super 100 * 2 via invoke-super

    // get() reads the field written by the chained constructors.
    const auto get_index =
        vm.linker.FindVtableIndex(*doubler_class, "get", "()I");
    REQUIRE(get_index.has_value());
    ExpectInt(vm.interpreter.Call(
                  vm.linker.Class(*doubler_class).vtable[*get_index],
                  std::vector<VmValue>{VmValue::Ref(instance)}),
              4);
    });
}

TEST_CASE("dexvm clinit runs once before static access") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized >= 1);
    const auto before = vm.interpreter.Stats().classes_initialized;
    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    CHECK(vm.interpreter.Stats().classes_initialized == before);
    // Static initial value from encoded_array.
    const auto counter_class = vm.linker.FindClass("LCounter;");
    REQUIRE(counter_class.has_value());
    const auto outcome =
        vm.interpreter.EnsureClassInitialized(*counter_class);
    REQUIRE(!outcome.exception.IsValid());
    CHECK(vm.linker.Class(*counter_class).static_storage[0] == 7);
    });
}

TEST_CASE("dexvm exceptions across frames with real messages") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    // Custom VM exception subclass: caught by Exception handler in the
    // caller frame; message length of "boom" is 4.
    ExpectInt(vm.CallStatic("LThrower;", "catchAcrossFrames", "()I"), 4);

    const auto uncaught = vm.CallStatic("LThrower;", "uncaught", "()I");
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(uncaught.exception_message == "boom");
    REQUIRE(!uncaught.exception_stack.empty());
    CHECK(uncaught.exception_stack[0].method_name == "fail");
    });
}

TEST_CASE("dexvm stack overflow is a real catchable error") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    const auto outcome =
        vm.CallStatic("LFlow;", "recurse", "(I)I", {VmValue::Int(0)});
    ExpectException(vm, outcome, "Ljava/lang/StackOverflowError;");
    });
}

TEST_CASE("dexvm tick budget exhaustion is a fatal structured error") {
    WithEachBackend([](InterpreterConfig config) {
    config.tick_budget = 100;
    Vm vm(config);
    CHECK_THROWS_AS(static_cast<void>(vm.CallStatic(
                        "LFlow;", "recurse", "(I)I", {VmValue::Int(0)})),
                    DexVmError);
    });
}

TEST_CASE("dexvm heap budget exhaustion surfaces OutOfMemoryError") {
    WithEachBackend([](InterpreterConfig config) {
    JavaObjectModelConfig model_config;
    model_config.initial_heap_target_bytes = 40;
    model_config.heap_growth_limit_bytes = 40;
    model_config.maximum_heap_bytes = 40;
    model_config.min_free_bytes = 0;
    model_config.max_free_bytes = 0;
    Vm vm(config, model_config);
    const auto outcome = vm.CallStatic("LFlow;", "sumArray", "()I");
    ExpectException(vm, outcome, "Ljava/lang/OutOfMemoryError;");
    });
}

TEST_CASE("dexvm heap grows after collection before reporting exhaustion") {
    WithEachBackend([](InterpreterConfig config) {
    JavaObjectModelConfig heap;
    heap.initial_heap_target_bytes = 40;
    heap.heap_growth_limit_bytes = 256;
    heap.maximum_heap_bytes = 512;
    heap.min_free_bytes = 0;
    heap.max_free_bytes = 32;
    Vm vm(config, heap);
    ExpectInt(vm.CallStatic("LFlow;", "sumArray", "()I"), 24);
    CHECK(vm.model.HeapTargetBytes() > heap.initial_heap_target_bytes);
    CHECK(vm.model.HeapTargetBytes() <= heap.heap_growth_limit_bytes);
    CHECK(vm.interpreter.Stats().gc_collections > 0);
    });
}

TEST_CASE("dexvm target utilization bounds allocation after collection") {
    WithEachBackend([](InterpreterConfig config) {
    JavaObjectModelConfig enabled;
    enabled.initial_heap_target_bytes = 96;
    enabled.heap_growth_limit_bytes = 128;
    enabled.maximum_heap_bytes = 128;
    enabled.min_free_bytes = 16;
    enabled.max_free_bytes = 32;
    Vm collecting(config, enabled);
    ExpectInt(collecting.CallStatic("LFlow;", "gcChurn", "()I"), 10);
    CHECK(collecting.interpreter.Stats().gc_collections > 0);
    CHECK(collecting.interpreter.Stats().gc_freed_bytes > 0);
    CHECK(collecting.model.AllocatedBytes() <= enabled.heap_growth_limit_bytes);
    });
}

TEST_CASE("dexvm execution contexts isolate mutable interpreter state") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    const auto first = vm.interpreter.CreateExecutionContext();
    const auto second = vm.interpreter.CreateExecutionContext();

    const auto loop = vm.Static("LFlow;", "loopSum", "(I)I");
    ExpectInt(vm.interpreter.Call(
                  first, loop, std::vector<VmValue>{VmValue::Int(10)}),
              45);
    const auto first_after_loop = vm.interpreter.ExecutionSnapshot(first);
    CHECK(first_after_loop.frame_depth == 0);
    CHECK(first_after_loop.ticks > 0);

    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(3)}),
              3);
    const auto second_after_loop = vm.interpreter.ExecutionSnapshot(second);
    CHECK(second_after_loop.frame_depth == 0);
    CHECK(second_after_loop.ticks > 0);
    CHECK(vm.interpreter.ExecutionSnapshot(first).ticks ==
          first_after_loop.ticks);

    const auto uncaught = vm.interpreter.Call(
        first, vm.Static("LThrower;", "uncaught", "()I"), {});
    ExpectException(vm, uncaught, "LMyError;");
    CHECK(!vm.interpreter.ExecutionSnapshot(first).has_pending_exception);
    ExpectInt(vm.interpreter.Call(
                  second, loop, std::vector<VmValue>{VmValue::Int(2)}),
              1);
    CHECK(!vm.interpreter.ExecutionSnapshot(second).has_pending_exception);

    const auto retain = vm.Static("LExecutionContextProbe;", "retainMonitor",
                                  "()V");
    const auto retained = vm.interpreter.Call(first, retain, {});
    CHECK(!retained.exception.IsValid());
    CHECK(vm.interpreter.ExecutionSnapshot(first).held_monitor_count == 1);
    CHECK(vm.interpreter.ExecutionSnapshot(second).held_monitor_count == 0);

    Vm other;
    CHECK_THROWS_AS(
        static_cast<void>(other.interpreter.ExecutionSnapshot(first)),
        DexVmError);
    });
}

TEST_CASE("DVM-234 bytecode handoff preserves monitors and live frame roots") {
    WithEachBackend([](InterpreterConfig config) {
        std::atomic<bool> started{};
        auto host = IntrinsicClassBuilder::Class("Lcleanup/Host;");
        host.StaticMethod("started", "()V", [&](IntrinsicContext&) {
            started.store(true);
            return VmValue::Void();
        });
        auto catalog = AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>());
        catalog.push_back(std::move(host).Build());
        Vm fixture(config, {}, std::move(catalog), "cooperative_cleanup.dex");
        auto& vm = fixture.interpreter;
        const auto object = fixture.model.NewInstance(
            *fixture.linker.FindClass("Ljava/lang/Object;"), 0);
        const auto context = vm.CreateExecutionContext();
        const auto spin = fixture.Static("LCooperativeCleanup;", "spin", "(Ljava/lang/Object;)I");
        const auto release = fixture.Static("LCooperativeCleanup;", "release", "()V");
        std::exception_ptr failure;
        VmCallOutcome outcome;
        std::thread worker([&] {
            try { outcome = vm.Call(context, spin, std::array{VmValue::Ref(object)}); }
            catch (...) { failure = std::current_exception(); }
        });
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!started.load() && std::chrono::steady_clock::now() < limit)
            std::this_thread::yield();
        CHECK(started.load());
        if (started.load()) {
            const VmExecutionLockScope lock(vm.ExecutionLock());
            CHECK(vm.Monitors().IsOwner(object, context.Token()));
            CHECK(vm.Monitors().Snapshot(object).recursion == 2);
            CHECK(vm.ExecutionSnapshot(context).ticks > 0);
            static_cast<void>(vm.CollectGarbage("cooperative-handoff"));
            CHECK(fixture.model.ObjectClass(object) == *fixture.linker.FindClass("Ljava/lang/Object;"));
            CHECK_FALSE(vm.Call(release, {}).exception.IsValid());
        } else vm.RequestStop(context);
        worker.join();
        if (failure) std::rethrow_exception(failure);
        CHECK(outcome.value.AsInt() == 7);
        CHECK_FALSE(vm.Monitors().IsOwner(object, context.Token()));
        vm.DiscardExecutionContext(context);
    });
}

TEST_CASE("DVM-234 Java loops honor a non Java lifecycle deadline on both backends") {
    WithEachBackend([](InterpreterConfig config) {
        Vm fixture(config, {}, AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>()));
        const auto begin = ogplay::hal::Clock::SteadyTimestampNs();
        CHECK_THROWS_WITH_AS(fixture.interpreter.RunWithExecutionDeadline(
            begin + 20'000'000, [&] {
                static_cast<void>(fixture.CallStatic("LFlow;", "loopSum", "(I)I",
                    {VmValue::Int(std::numeric_limits<std::int32_t>::max())}));
            }), doctest::Contains("lifecycle execution wall-time budget exhausted"), DexVmError);
        CHECK(ogplay::hal::Clock::SteadyTimestampNs() - begin < 1'000'000'000);
        CHECK_FALSE(fixture.interpreter.HasExecutionDeadline());
        ExpectInt(fixture.CallStatic("LFlow;", "loopSum", "(I)I", {VmValue::Int(10)}), 45);
    });
}

TEST_CASE("DVM-234 foreign GC defers guest cleanup while root execution is yielded") {
    WithEachBackend([](InterpreterConfig config) {
        std::atomic<bool> started{};
        std::size_t cleaned{};
        auto host = IntrinsicClassBuilder::Class("Lcleanup/Host;");
        host.StaticMethod("started", "()V", [&](IntrinsicContext&) {
            started.store(true);
            return VmValue::Void();
        });
        host.StaticMethod("cleanup", "(J)V", [&](IntrinsicContext& call) {
            CHECK(call.arguments[0].AsLong() == 123);
            ++cleaned;
            return VmValue::Void();
        });
        auto catalog = AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>());
        catalog.push_back(std::move(host).Build());
        Vm fixture(config, {}, std::move(catalog), "cooperative_cleanup.dex");
        auto& vm = fixture.interpreter;
        const auto klass = *fixture.linker.FindClass("Ljava/lang/Object;");
        const auto monitor = fixture.model.NewInstance(klass, 0);
        const auto resource = fixture.model.NewInstance(klass, 0);
        vm.TrackGuestNativeResource(resource,
            fixture.Static("Lcleanup/Host;", "cleanup", "(J)V"), 123);
        const auto spin = fixture.Static("LCooperativeCleanup;", "spin", "(Ljava/lang/Object;)I");
        const auto release = fixture.Static("LCooperativeCleanup;", "release", "()V");
        const auto peer = vm.CreateExecutionContext();
        std::exception_ptr failure;
        std::thread root([&] {
            try { CHECK(vm.Call(spin, std::array{VmValue::Ref(monitor)}).value.AsInt() == 7); }
            catch (...) { failure = std::current_exception(); }
        });
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!started.load() && std::chrono::steady_clock::now() < limit)
            std::this_thread::yield();
        CHECK(started.load());
        if (started.load()) {
            const VmExecutionLockScope lock(vm.ExecutionLock());
            static_cast<void>(vm.CollectGarbage("foreign-host-allocation"));
            CHECK(cleaned == 0);
            CHECK(vm.GuestNativeResourceCount() == 1);
            CHECK_FALSE(vm.Call(peer, release, {}).exception.IsValid());
        } else vm.RequestStop(vm.ExecutionContext(1));
        root.join();
        if (failure) std::rethrow_exception(failure);
        CHECK_NOTHROW(vm.ReleaseGuestNativeResources());
        CHECK(cleaned == 1);
        CHECK(vm.GuestNativeResourceCount() == 0);
        vm.DiscardExecutionContext(peer);
    });
}

TEST_CASE("DVM-234 idle production root cannot be borrowed for foreign GC cleanup") {
    WithEachBackend([](InterpreterConfig config) {
        std::size_t cleaned{};
        auto host = IntrinsicClassBuilder::Class("Lcleanup/Host;");
        host.StaticMethod("cleanup", "(J)V", [&](IntrinsicContext&) {
            ++cleaned;
            return VmValue::Void();
        });
        auto catalog = AndroidIntrinsicCatalog(std::make_shared<DexVmAndroidContext>());
        catalog.push_back(std::move(host).Build());
        Vm fixture(config, {}, std::move(catalog), "cooperative_cleanup.dex");
        auto& vm = fixture.interpreter;
        vm.BindDefaultExecutionHost();
        const auto resource = fixture.model.NewInstance(
            *fixture.linker.FindClass("Ljava/lang/Object;"), 0);
        const auto cleanup = fixture.Static("Lcleanup/Host;", "cleanup", "(J)V");
        vm.TrackGuestNativeResource(resource, cleanup, 123);
        std::exception_ptr failure;
        std::thread collector([&] {
            try {
                static_cast<void>(vm.CollectGarbage("idle-foreign-allocation"));
                CHECK_THROWS_AS(static_cast<void>(vm.Call(cleanup, std::array{VmValue::Long(123)})), DexVmError);
            } catch (...) { failure = std::current_exception(); }
        });
        collector.join();
        if (failure) std::rethrow_exception(failure);
        CHECK(cleaned == 0);
        CHECK(vm.GuestNativeResourceCount() == 1);
        vm.ReleaseGuestNativeResources();
        CHECK(cleaned == 1);
        CHECK(vm.GuestNativeResourceCount() == 0);
    });
}
