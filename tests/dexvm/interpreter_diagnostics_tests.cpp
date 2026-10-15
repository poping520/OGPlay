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

namespace {

IntrinsicClassDecl DiagnosticSystemPropertiesOverlay() {
    auto builder =
        IntrinsicClassBuilder::Class("Landroid/os/SystemProperties;");
    constexpr auto flags = kAccPrivate | kAccNative;
    builder.StaticMethod(
        "native_get", "(Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); }, flags);
    builder.StaticMethod(
        "native_get",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        [](IntrinsicContext&) { return VmValue::Ref(VmObjectRef{}); }, flags);
    builder.StaticMethod(
        "native_get_int", "(Ljava/lang/String;I)I",
        [](IntrinsicContext&) { return VmValue::Int(0); }, flags);
    builder.StaticMethod(
        "native_get_long", "(Ljava/lang/String;J)J",
        [](IntrinsicContext&) { return VmValue::Long(0); }, flags);
    builder.StaticMethod(
        "native_get_boolean", "(Ljava/lang/String;Z)Z",
        [](IntrinsicContext&) { return VmValue::Int(0); }, flags);
    builder.StaticMethod(
        "native_set", "(Ljava/lang/String;Ljava/lang/String;)V",
        [](IntrinsicContext&) { return VmValue::Void(); }, flags);
    builder.StaticMethod(
        "native_add_change_callback", "()V",
        [](IntrinsicContext&) { return VmValue::Void(); }, flags);
    return std::move(builder).Build();
}

} // namespace

TEST_CASE("dexvm diagnostics stay disabled unless explicitly configured") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    CHECK_FALSE(vm.interpreter.DiagnosticsEnabled());
    ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                            {VmValue::Int(12), VmValue::Int(3)}),
              4);
    CHECK(vm.interpreter.Trace("", 16).empty());

    const auto second = vm.interpreter.CreateExecutionContext();
    const auto stacks = vm.interpreter.StackSnapshot();
    REQUIRE(stacks.size() == 2);
    CHECK(stacks[0].context_token != 0);
    CHECK(stacks[1].context_token == second.Token());
    CHECK(stacks[0].thread_status == "idle");
    CHECK(stacks[1].thread_status == "idle");
    CHECK(stacks[0].frames.empty());
    CHECK(stacks[1].frames.empty());

    std::vector<DexVmThreadStack> live_stacks;
    auto builder = IntrinsicClassBuilder::Class("Ldiagnostics/Host;");
    builder.StaticMethod("capture", "()I",
                         [&live_stacks](IntrinsicContext& context) {
                             live_stacks = context.vm.StackSnapshot();
                             const auto found = std::find_if(
                                 live_stacks.begin(), live_stacks.end(),
                                 [](const auto& stack) {
                                     return !stack.frames.empty();
                                 });
                             return VmValue::Int(
                                 found == live_stacks.end()
                                     ? 0
                                     : static_cast<std::int32_t>(
                                           found->frames.size()));
                         });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    Vm live(config, JavaObjectModelConfig{}, std::move(catalog));
    ExpectInt(live.CallStatic("LDiagnosticsProbe;", "capture", "()I"), 1);
    const auto running = std::find_if(
        live_stacks.begin(), live_stacks.end(),
        [](const auto& stack) { return !stack.frames.empty(); });
    REQUIRE(running != live_stacks.end());
    REQUIRE(running->frames.size() == 1);
    CHECK(running->thread_status == "running");
    CHECK(running->frames[0].class_descriptor == "LDiagnosticsProbe;");
    CHECK(running->frames[0].method_name == "capture");
    CHECK(running->frames[0].method_descriptor == "()I");
    });
}

TEST_CASE("dexvm fatal errors retain the interpreted guest call stack") {
    WithEachBackend([](InterpreterConfig config) {
        auto builder =
            IntrinsicClassBuilder::Class("Ldiagnostics/Host;");
        std::vector<IntrinsicClassDecl> catalog;
        catalog.push_back(DiagnosticSystemPropertiesOverlay());
        catalog.push_back(std::move(builder).Build());
        Vm vm(config, JavaObjectModelConfig{}, std::move(catalog));
        VmThreadRuntime threads(vm.interpreter);
        threads.SetRootThreadObject(
            vm.interpreter.NewIntrinsicInstance("Ljava/lang/Thread;"));

        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureOuter", "()I"));
            FAIL("missing intrinsic method must fail");
        } catch (const DexVmError& error) {
            CHECK(error.Reason() == DexVmErrorReason::unresolved_reference);
            const std::string message = error.what();
            CHECK(message.find(
                      "method cannot be resolved: "
                      "Ldiagnostics/Host;->capture()I") != std::string::npos);
            CHECK(message.find(
                      "context=1 guest_thread_id=1 thread=\"main\" "
                      "frames=2 shown=2") != std::string::npos);
            CHECK(message.find(
                      "DexVM fault instruction: invoke-static (opcode=0x71, "
                      "method_idx=") != std::string::npos);
            const auto inner = message.find(
                "#0 at LDiagnosticsProbe;->capture()I (dex_pc=0)");
            const auto outer = message.find(
                "#1 at LDiagnosticsProbe;->captureOuter()I (dex_pc=0)");
            REQUIRE(inner != std::string::npos);
            REQUIRE(outer != std::string::npos);
            CHECK(inner < outer);
            CHECK(message.find("DexVM guest stack (innermost first):",
                               inner + 1U) == std::string::npos);
        }

        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureDeep", "(I)I",
                {VmValue::Int(70)}));
            FAIL("deep missing intrinsic method must fail");
        } catch (const DexVmError& error) {
            const std::string message = error.what();
            CHECK(message.find("frames=71 shown=64") != std::string::npos);
            CHECK(message.find("#63 at LDiagnosticsProbe;->captureDeep(I)I") !=
                  std::string::npos);
            CHECK(message.find("#64 at ") == std::string::npos);
            CHECK(message.find("... 7 outer frames omitted") !=
                  std::string::npos);
        }
        const auto stacks = vm.interpreter.StackSnapshot();
        REQUIRE(stacks.size() == 1U);
        CHECK(stacks.front().frames.empty());
    });
}

TEST_CASE("dexvm fatal invoke diagnostics render bounded typed arguments") {
    WithEachBackend([](InterpreterConfig config) {
        auto builder =
            IntrinsicClassBuilder::Class("Ldiagnostics/Host;");
        builder.FinalOverrideMethod(
            "toString", "()Ljava/lang/String;",
            [](IntrinsicContext& call) {
                return VmValue::Ref(call.vm.NewStringUtf8("host-view"));
            });
        std::vector<IntrinsicClassDecl> catalog;
        catalog.push_back(DiagnosticSystemPropertiesOverlay());
        catalog.push_back(std::move(builder).Build());
        Vm vm(config, JavaObjectModelConfig{}, std::move(catalog));

        const auto text = vm.interpreter.NewStringUtf8("query\n\"value\"");
        const auto receiver =
            vm.interpreter.NewIntrinsicInstance("Ldiagnostics/Host;");
        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureVirtual",
                "(Ldiagnostics/Host;Ljava/lang/String;)I",
                {VmValue::Ref(receiver), VmValue::Ref(text)}));
            FAIL("missing virtual method must fail");
        } catch (const DexVmError& error) {
            const std::string message = error.what();
            CHECK(message.find(
                      "DexVM fault invoke:\n"
                      "  target   : Ldiagnostics/Host;->missing\n"
                      "  returns  : I") != std::string::npos);
            CHECK(message.find("DexVM fault invoke:") <
                  message.find("DexVM guest stack (innermost first):"));
            CHECK(message.find(
                      "receiver : Ldiagnostics/Host; = \"host-view\"") !=
                  std::string::npos);
            CHECK(message.find(
                      "[ 0] Ljava/lang/String;           = "
                      "\"query\\n\\\"value\\\"\"") != std::string::npos);
        }

        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureVirtual",
                "(Ldiagnostics/Host;Ljava/lang/String;)I",
                {VmValue::Ref(receiver), VmValue::Ref(VmObjectRef{})}));
            FAIL("missing virtual method must fail");
        } catch (const DexVmError& error) {
            CHECK(std::string(error.what()).find(
                      "[ 0] Ljava/lang/String;           = null") !=
                  std::string::npos);
        }

        const auto string_class =
            vm.linker.ResolveDescriptor("Ljava/lang/String;");
        const auto array_class =
            vm.linker.ResolveDescriptor("[Ljava/lang/String;");
        const auto values = vm.model.NewObjectArray(array_class, string_class, 2);
        vm.model.SetObjectElement(values, 0,
                                  vm.interpreter.NewStringUtf8("first"));
        vm.model.SetObjectElement(values, 1,
                                  vm.interpreter.NewStringUtf8("second"));
        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureArgsRange",
                "(Ljava/lang/String;FIZBSCJD[Ljava/lang/String;)I",
                {VmValue::Ref(text), VmValue::Float(1.5F), VmValue::Int(-7),
                 VmValue::Int(1), VmValue::Int(-2), VmValue::Int(-300),
                 VmValue::Int(65),
                 VmValue::Long(INT64_C(4294967297)), VmValue::Double(-2.25),
                 VmValue::Ref(values)}));
            FAIL("missing range method must fail");
        } catch (const DexVmError& error) {
            const std::string message = error.what();
            CHECK(message.find("DexVM fault instruction: invoke-static/range "
                               "(opcode=0x77,") != std::string::npos);
            CHECK(message.find("[ 0] Ljava/lang/String;           = "
                               "\"query\\n\\\"value\\\"\"") !=
                  std::string::npos);
            CHECK(message.find("[ 1] F                            = 1.5") !=
                  std::string::npos);
            CHECK(message.find("[ 2] I                            = -7") !=
                  std::string::npos);
            CHECK(message.find("[ 3] Z                            = true") !=
                  std::string::npos);
            CHECK(message.find("[ 4] B                            = -2") !=
                  std::string::npos);
            CHECK(message.find("[ 5] S                            = -300") !=
                  std::string::npos);
            CHECK(message.find("[ 6] C                            = 65") !=
                  std::string::npos);
            CHECK(message.find("[ 7] J                            = 4294967297") !=
                  std::string::npos);
            CHECK(message.find("[ 8] D                            = -2.25") !=
                  std::string::npos);
            CHECK(message.find(
                      "[ 9] [Ljava/lang/String;          = "
                      "\"[Ljava/lang/String;@") != std::string::npos);
        }

        const auto long_text = vm.interpreter.NewStringUtf8(
            std::string(120U, 'x'));
        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureVirtual",
                "(Ldiagnostics/Host;Ljava/lang/String;)I",
                {VmValue::Ref(receiver), VmValue::Ref(long_text)}));
            FAIL("missing virtual method must fail");
        } catch (const DexVmError& error) {
            CHECK(std::string(error.what()).find(
                      std::string("[ 0] Ljava/lang/String;           = \"") +
                      std::string(96U, 'x') + "\"... (length=120)") !=
                  std::string::npos);
        }

        std::vector<VmValue> many;
        for (std::int32_t value = 0; value < 17; ++value) {
            many.push_back(VmValue::Int(value));
        }
        try {
            static_cast<void>(vm.CallStatic(
                "LDiagnosticsProbe;", "captureMany",
                "(IIIIIIIIIIIIIIIII)I", std::move(many)));
            FAIL("missing many-argument method must fail");
        } catch (const DexVmError& error) {
            const std::string message = error.what();
            CHECK(message.find("[15] I                            = 15") !=
                  std::string::npos);
            CHECK(message.find("[16] I                            = 16") ==
                  std::string::npos);
            CHECK(message.find("... 1 arguments omitted") !=
                  std::string::npos);
        }
    });
}

TEST_CASE("dexvm parked execution contexts reject foreign host reentry") {
    WithEachBackend([](InterpreterConfig config) {
        std::mutex mutex;
        std::condition_variable changed;
        bool parked = false;
        bool release = false;
        auto b = IntrinsicClassBuilder::Class("Ltest/ParkedExecution;", "Ljava/lang/Object;");
        b.StaticMethod("park", "()J", [&](IntrinsicContext& call) {
            const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
            {
                std::unique_lock lock(mutex);
                parked = true;
                changed.notify_all();
                changed.wait(lock, [&] { return release; });
            }
            call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
            return VmValue::Long(INT64_C(0x123456789));
        });
        Vm vm(config, {}, {std::move(b).Build()});
        const auto park = vm.Static("Ltest/ParkedExecution;", "park", "()J");
        const auto divide = vm.Static("LArith;", "divide", "(II)I");
        const auto independent = vm.interpreter.CreateExecutionContext();
        VmCallOutcome owner_result;
        std::exception_ptr owner_error;
        std::thread owner([&] {
            try { owner_result = vm.interpreter.Call(park, {}); }
            catch (...) { owner_error = std::current_exception(); }
        });
        {
            std::unique_lock lock(mutex);
            changed.wait(lock, [&] { return parked; });
        }
        const std::array arguments{VmValue::Int(12), VmValue::Int(3)};
        bool rejected = false;
        try { static_cast<void>(vm.interpreter.Call(divide, arguments)); }
        catch (const DexVmError& error) {
            rejected = std::string(error.what()).find("active on another host thread") != std::string::npos;
        }
        CHECK_THROWS_AS(vm.interpreter.DiscardExecutionContext(
            vm.interpreter.ExecutionContext(1)), DexVmError);
        const auto result = vm.interpreter.Call(independent, divide, arguments);
        {
            std::lock_guard lock(mutex);
            release = true;
        }
        changed.notify_all();
        owner.join();
        CHECK(rejected);
        CHECK_FALSE(owner_error);
        ExpectInt(result, 4);
        REQUIRE_FALSE(owner_result.exception.IsValid());
        CHECK(owner_result.value.kind == VmValue::Kind::wide);
        CHECK(owner_result.value.AsLong() == INT64_C(0x123456789));
        ExpectInt(vm.interpreter.Call(divide, arguments), 4);
        vm.interpreter.DiscardExecutionContext(independent);
    });
}

TEST_CASE("dexvm diagnostic safe point reports busy instead of waiting") {
    WithEachBackend([](InterpreterConfig config) {
        config.diagnostics.trace_capacity = 8U;
        Vm vm(config);
        std::atomic<bool> locked{};
        std::atomic<bool> release{};
        std::thread owner([&] {
            VmExecutionLockScope execution(vm.interpreter.ExecutionLock());
            locked.store(true, std::memory_order_release);
            while (!release.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
        while (!locked.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        CHECK_FALSE(vm.interpreter.TryTrace(8U).has_value());
        CHECK_FALSE(vm.interpreter.TryStackSnapshot().has_value());
        release.store(true, std::memory_order_release);
        owner.join();
        CHECK(vm.interpreter.TryTrace(8U).has_value());
        CHECK(vm.interpreter.TryStackSnapshot().has_value());
    });
}

TEST_CASE("dexvm diagnostics use a bounded filtered event ring") {
    WithEachBackend([](InterpreterConfig config) {
    config.diagnostics.trace_capacity = 3;
    config.diagnostics.event_mask =
        DexVmTraceBit(DexVmTraceKind::method_enter) |
        DexVmTraceBit(DexVmTraceKind::method_exit);
    Vm vm(config);
    CHECK(vm.interpreter.DiagnosticsEnabled());

    for (int i = 0; i < 3; ++i) {
        ExpectInt(vm.CallStatic("LArith;", "divide", "(II)I",
                                {VmValue::Int(12), VmValue::Int(3)}),
                  4);
    }
    const auto trace = vm.interpreter.Trace("divide", 16);
    REQUIRE(trace.size() == 3);
    CHECK(trace[0].sequence > 1);
    CHECK(trace[0].sequence < trace[1].sequence);
    CHECK(trace[1].sequence < trace[2].sequence);
    for (const auto& entry : trace) {
        CHECK(entry.class_descriptor == "LArith;");
        CHECK(entry.method_name == "divide");
        CHECK(entry.method_descriptor == "(II)I");
        CHECK(entry.context_token != 0);
        CHECK((entry.kind == DexVmTraceKind::method_enter ||
               entry.kind == DexVmTraceKind::method_exit));
    }
    CHECK(vm.interpreter.Trace("does-not-exist", 16).empty());
    CHECK_THROWS_AS(static_cast<void>(vm.interpreter.Trace("", 0)),
                    std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(vm.interpreter.Trace("", 10001)),
                    std::invalid_argument);

    const auto json = RenderDexVmTraceJson(trace);
    CHECK(json.find("\"schema\":1") != std::string::npos);
    CHECK(json.find("\"event\":\"method_") != std::string::npos);
    CHECK(json.find("\"class\":\"LArith;\"") != std::string::npos);
    CHECK(json.find("0x") == std::string::npos);
    });
}

TEST_CASE("dexvm diagnostics sample only instruction events") {
    WithEachBackend([](InterpreterConfig config) {
    config.diagnostics.trace_capacity = 64;
    config.diagnostics.event_mask =
        DexVmTraceBit(DexVmTraceKind::instruction);
    config.diagnostics.instruction_sample_interval = 2;
    Vm vm(config);

    ExpectInt(vm.CallStatic("LFlow;", "loopSum", "(I)I",
                            {VmValue::Int(4)}),
              6);
    const auto trace = vm.interpreter.Trace("instruction", 64);
    REQUIRE(!trace.empty());
    for (const auto& entry : trace) {
        CHECK(entry.kind == DexVmTraceKind::instruction);
        CHECK(entry.tick % 2 == 0);
        CHECK(entry.class_descriptor == "LFlow;");
        CHECK(entry.method_name == "loopSum");
    }

    const auto stacks_json =
        RenderDexVmStacksJson(vm.interpreter.StackSnapshot());
    CHECK(stacks_json.find("\"schema\":1") != std::string::npos);
    CHECK(stacks_json.find("\"status\":\"idle\"") !=
          std::string::npos);
    });
}

TEST_CASE("dexvm diagnostics cover semantic fault and runtime events") {
    WithEachBackend([](InterpreterConfig config) {
    config.diagnostics.trace_capacity = 512;
    config.diagnostics.event_mask =
        kDexVmTraceAllEvents &
        ~DexVmTraceBit(DexVmTraceKind::instruction);
    JavaObjectModelConfig heap;
    heap.initial_heap_target_bytes = 65536;
    heap.heap_growth_limit_bytes = 131072;
    heap.maximum_heap_bytes = 131072;
    heap.min_free_bytes = 4096;
    heap.max_free_bytes = 16384;
    Vm vm(config, heap);

    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    ExpectInt(vm.CallStatic("LArith;", "divideCaught", "(II)I",
                            {VmValue::Int(1), VmValue::Int(0)}),
              -99);
    ExpectInt(vm.CallStatic("LFlow;", "gcChurn", "()I"), 10);
    const auto monitor = vm.CallStatic("LWaitProbe;", "signal", "()V");
    REQUIRE_FALSE(monitor.exception.IsValid());

    static_cast<void>(vm.interpreter.CollectGarbage("diagnostics-event"));
    std::set<DexVmTraceKind> observed;
    for (const auto& entry : vm.interpreter.Trace("", 512)) {
        observed.insert(entry.kind);
    }
    for (const auto kind : {DexVmTraceKind::class_init_begin,
                            DexVmTraceKind::class_init_end,
                            DexVmTraceKind::exception_throw,
                            DexVmTraceKind::exception_catch,
                            DexVmTraceKind::monitor_enter,
                            DexVmTraceKind::monitor_exit,
                            DexVmTraceKind::monitor_notify,
                            DexVmTraceKind::gc_begin,
                            DexVmTraceKind::gc_end}) {
        CAPTURE(DexVmTraceKindName(kind));
        CHECK(observed.contains(kind));
    }
    });
}

TEST_CASE("dexvm diagnostics reject invalid recorder configuration") {
    InterpreterConfig zero_sample;
    zero_sample.diagnostics.trace_capacity = 1;
    zero_sample.diagnostics.instruction_sample_interval = 0;
    CHECK_THROWS_AS(static_cast<void>(Vm(zero_sample)),
                    std::invalid_argument);

    InterpreterConfig excessive_ring;
    excessive_ring.diagnostics.trace_capacity = 1'000'001;
    CHECK_THROWS_AS(static_cast<void>(Vm(excessive_ring)),
                    std::invalid_argument);
}
