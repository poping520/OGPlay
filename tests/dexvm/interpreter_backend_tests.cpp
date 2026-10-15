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

void ReportThreadedMicrobenchmark(const char* tag,
                                  const char* owner,
                                  const char* name,
                                  const char* descriptor,
                                  const std::int32_t input,
                                  const std::int32_t expected) {
    InterpreterConfig threaded_config;
    threaded_config.backend = InterpreterBackend::threaded;
    Vm switch_vm;
    Vm threaded_vm(threaded_config);
    const std::vector<VmValue> arguments{VmValue::Int(input)};
    ExpectInt(switch_vm.CallStatic(owner, name, descriptor, arguments),
              expected);
    ExpectInt(threaded_vm.CallStatic(owner, name, descriptor, arguments),
              expected);
    constexpr std::uint32_t kIterations = 400;
    constexpr int kRounds = 5;
    const auto run = [&](Vm& vm) {
        std::int64_t checksum{};
        const auto start = std::chrono::steady_clock::now();
        for (std::uint32_t index = 0; index < kIterations; ++index) {
            const auto outcome =
                vm.CallStatic(owner, name, descriptor, arguments);
            REQUIRE_FALSE(outcome.exception.IsValid());
            checksum += outcome.value.AsInt();
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start);
        return std::pair{elapsed.count(), checksum};
    };
    static_cast<void>(run(switch_vm));
    static_cast<void>(run(threaded_vm));
    std::vector<std::int64_t> switch_samples;
    std::vector<std::int64_t> threaded_samples;
    switch_samples.reserve(kRounds);
    threaded_samples.reserve(kRounds);
    std::int64_t switch_sum{};
    std::int64_t threaded_sum{};
    for (int round = 0; round < kRounds; ++round) {
        const auto switch_run = run(switch_vm);
        const auto threaded_run = run(threaded_vm);
        switch_samples.push_back(switch_run.first);
        threaded_samples.push_back(threaded_run.first);
        switch_sum = switch_run.second;
        threaded_sum = threaded_run.second;
    }
    CHECK(threaded_sum == switch_sum);
    const auto median_of = [](std::vector<std::int64_t> samples) {
        std::sort(samples.begin(), samples.end());
        return samples[samples.size() / 2];
    };
    const auto switch_us = median_of(std::move(switch_samples));
    const auto threaded_us = median_of(std::move(threaded_samples));
    std::cout << tag << " switch_us=" << switch_us
              << " threaded_us=" << threaded_us;
    if (switch_us > 0) {
        const auto delta_pct =
            (100.0 * static_cast<double>(switch_us - threaded_us)) /
            static_cast<double>(switch_us);
        std::cout << " delta_pct=" << delta_pct;
    }
    std::cout << '\n';
}

void InstallMalformedCode(Vm& vm, const std::string& owner,
                          const std::string& name,
                          const std::string& descriptor,
                          const std::uint16_t registers,
                          std::initializer_list<std::uint16_t> units) {
    const auto id = vm.Static(owner, name, descriptor);
    auto& method = vm.linker.MutableMethod(id);
    method.prechecked = false;
    method.fast_code.reset();
    REQUIRE(method.code.has_value());
    method.code->info.registers_size = registers;
    method.code->instructions.assign(units);
}

void ExpectInvalidRegister(Vm& vm, const std::string& owner,
                           const std::string& name,
                           const std::string& descriptor,
                           std::vector<VmValue> arguments) {
    try {
        static_cast<void>(vm.CallStatic(owner, name, descriptor,
                                        std::move(arguments)));
        FAIL("expected DexVmError for out-of-range registers");
    } catch (const DexVmError& error) {
        CHECK(error.Reason() == DexVmErrorReason::invalid_register);
        CHECK(std::string(error.what()).find("register out of range") !=
              std::string::npos);
    }
}

void ExpectMatchingStructuralDiagnostic(
    std::initializer_list<std::uint16_t> units, const char* detail) {
    Vm vm;
    const auto id = vm.Static("LFlow;", "loopSum", "(I)I");
    auto& method = vm.linker.MutableMethod(id);
    method.prechecked = false;
    method.fast_code.reset();
    REQUIRE(method.code.has_value());
    method.code->instructions.assign(units);
    const auto where = std::string("LFlow;.loopSum");
    std::string precheck_what;
    DexVmErrorReason precheck_reason{};
    try {
        vm.linker.PrecheckMethod(id);
        FAIL("expected PrecheckMethod to reject");
    } catch (const DexVmError& error) {
        precheck_what = error.what();
        precheck_reason = error.Reason();
    }
    try {
        static_cast<void>(BuildFastCode(*method.code, where));
        FAIL("expected BuildFastCode to reject");
    } catch (const DexVmError& error) {
        CHECK(std::string(error.what()) == precheck_what);
        CHECK(error.Reason() == precheck_reason);
        CHECK(precheck_what == where + ": " + detail);
    }
}

struct CapturedDexVmError final {
    DexVmErrorReason reason{};
    std::string what;
};

template <typename Patch>
CapturedDexVmError CaptureInvokeWideError(const InterpreterBackend backend,
                                          Patch&& patch) {
    InterpreterConfig config;
    config.backend = backend;
    Vm vm(config);
    const auto id = vm.Static("LWideArg;", "call", "()I");
    auto& method = vm.linker.MutableMethod(id);
    REQUIRE(method.code.has_value());
    patch(method);
    method.prechecked = false;
    method.fast_code.reset();
    try {
        static_cast<void>(vm.interpreter.Call(id, {}));
        FAIL("expected DexVmError for malformed wide invoke");
    } catch (const DexVmError& error) {
        return {error.Reason(), error.what()};
    }
    return {};
}

[[nodiscard]] bool PatchOpcodeRegisterWord(std::vector<std::uint16_t>& units,
                                           const std::uint8_t opcode,
                                           const std::uint16_t regs_word) {
    for (std::size_t index = 0; index + 2U < units.size(); ++index) {
        if (static_cast<std::uint8_t>(units[index] & 0xffU) == opcode) {
            units[index + 2U] = regs_word;
            return true;
        }
    }
    return false;
}



} // namespace

TEST_CASE("dexvm threaded all-bridge backend matches switch semantics") {
    InterpreterConfig switch_config;
    switch_config.diagnostics.trace_capacity = 512;
    InterpreterConfig threaded_config = switch_config;
    threaded_config.backend = InterpreterBackend::threaded;
    threaded_config.force_all_bridge = true;
    Vm switch_vm(switch_config);
    Vm threaded_vm(threaded_config);

    const auto compare_int = [&](const std::string& owner,
                                 const std::string& name,
                                 const std::string& descriptor,
                                 const std::vector<VmValue>& arguments) {
        const auto expected =
            switch_vm.CallStatic(owner, name, descriptor, arguments);
        const auto actual =
            threaded_vm.CallStatic(owner, name, descriptor, arguments);
        REQUIRE_FALSE(expected.exception.IsValid());
        REQUIRE_FALSE(actual.exception.IsValid());
        CHECK(actual.value.kind == expected.value.kind);
        CHECK(actual.value.cat1 == expected.value.cat1);
        CHECK(actual.value.wide == expected.value.wide);
    };
    compare_int("LFlow;", "loopSum", "(I)I", {VmValue::Int(20)});
    compare_int("LFlow;", "sparse", "(I)I", {VmValue::Int(1000)});
    compare_int("LArith;", "divide", "(II)I",
                {VmValue::Int(-91), VmValue::Int(7)});
    compare_int("LArith;", "shifts", "(II)I",
                {VmValue::Int(3), VmValue::Int(33)});
    compare_int("LFlow;", "sumArray", "()I", {});
    compare_int("LArrayLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LSwitchLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LInstanceLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LVirtualLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LWideLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LWideArg;", "call", "()I", {});
    compare_int("LWideArg;", "callRange", "()I", {});
    compare_int("LTypeLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LTask;", "exercise", "()I", {});
    compare_int("LTypes;", "isRunnable", "()I", {});
    compare_int("LCloneProbe;", "cloneFields", "()I", {});
    compare_int("LClinitUser;", "read", "()I", {});

    const auto expected_exception =
        switch_vm.CallStatic("LThrower;", "uncaught", "()I");
    const auto actual_exception =
        threaded_vm.CallStatic("LThrower;", "uncaught", "()I");
    REQUIRE(expected_exception.exception.IsValid());
    REQUIRE(actual_exception.exception.IsValid());
    CHECK(switch_vm.linker.Class(expected_exception.exception_class).descriptor ==
          threaded_vm.linker.Class(actual_exception.exception_class).descriptor);
    CHECK(actual_exception.exception_message ==
          expected_exception.exception_message);

    CHECK(threaded_vm.interpreter.Stats().backend ==
          InterpreterBackend::threaded);
    CHECK(switch_vm.interpreter.Stats().executed_instructions ==
          threaded_vm.interpreter.Stats().executed_instructions);
    CHECK(threaded_vm.interpreter.Stats().fast_code_builds == 0);
    CHECK(switch_vm.interpreter.Stats().fast_code_builds == 0);

    const auto switch_trace = switch_vm.interpreter.Trace("instruction", 512);
    const auto threaded_trace =
        threaded_vm.interpreter.Trace("instruction", 512);
    REQUIRE(switch_trace.size() == threaded_trace.size());
    for (std::size_t index = 0; index < switch_trace.size(); ++index) {
        CHECK(switch_trace[index].tick == threaded_trace[index].tick);
        CHECK(switch_trace[index].dex_pc == threaded_trace[index].dex_pc);
        CHECK(switch_trace[index].opcode == threaded_trace[index].opcode);
    }
}

TEST_CASE("dexvm threaded direct handlers match switch ticks") {
    InterpreterConfig switch_config;
    switch_config.diagnostics.trace_capacity = 512;
    InterpreterConfig threaded_config = switch_config;
    threaded_config.backend = InterpreterBackend::threaded;
    Vm switch_vm(switch_config);
    Vm threaded_vm(threaded_config);

    const auto compare_int = [&](const std::string& owner,
                                 const std::string& name,
                                 const std::string& descriptor,
                                 const std::vector<VmValue>& arguments) {
        const auto expected =
            switch_vm.CallStatic(owner, name, descriptor, arguments);
        const auto actual =
            threaded_vm.CallStatic(owner, name, descriptor, arguments);
        REQUIRE_FALSE(expected.exception.IsValid());
        REQUIRE_FALSE(actual.exception.IsValid());
        CHECK(actual.value.kind == expected.value.kind);
        CHECK(actual.value.cat1 == expected.value.cat1);
        CHECK(actual.value.wide == expected.value.wide);
    };
    compare_int("LFlow;", "loopSum", "(I)I", {VmValue::Int(20)});
    compare_int("LFlow;", "sparse", "(I)I", {VmValue::Int(1000)});
    compare_int("LArith;", "divide", "(II)I",
                {VmValue::Int(-91), VmValue::Int(7)});
    compare_int("LFlow;", "sumArray", "()I", {});
    compare_int("LArrayLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LSwitchLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LInstanceLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LVirtualLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LWideLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LWideArg;", "call", "()I", {});
    compare_int("LWideArg;", "callRange", "()I", {});
    compare_int("LTypeLoop;", "run", "(I)I", {VmValue::Int(20)});
    compare_int("LTask;", "exercise", "()I", {});
    compare_int("LClinitUser;", "read", "()I", {});

    const auto expected_exception =
        switch_vm.CallStatic("LThrower;", "uncaught", "()I");
    const auto actual_exception =
        threaded_vm.CallStatic("LThrower;", "uncaught", "()I");
    REQUIRE(expected_exception.exception.IsValid());
    REQUIRE(actual_exception.exception.IsValid());
    CHECK(switch_vm.linker.Class(expected_exception.exception_class).descriptor ==
          threaded_vm.linker.Class(actual_exception.exception_class).descriptor);
    CHECK(actual_exception.exception_message ==
          expected_exception.exception_message);

    CHECK(threaded_vm.interpreter.Stats().backend ==
          InterpreterBackend::threaded);
    CHECK(switch_vm.interpreter.Stats().executed_instructions ==
          threaded_vm.interpreter.Stats().executed_instructions);
    CHECK(threaded_vm.interpreter.Stats().fast_code_builds > 0);
    CHECK(switch_vm.interpreter.Stats().fast_code_builds == 0);

    const auto switch_trace = switch_vm.interpreter.Trace("instruction", 512);
    const auto threaded_trace =
        threaded_vm.interpreter.Trace("instruction", 512);
    REQUIRE(switch_trace.size() == threaded_trace.size());
    for (std::size_t index = 0; index < switch_trace.size(); ++index) {
        CHECK(switch_trace[index].tick == threaded_trace[index].tick);
        CHECK(switch_trace[index].dex_pc == threaded_trace[index].dex_pc);
        CHECK(switch_trace[index].opcode == threaded_trace[index].opcode);
    }
}

TEST_CASE("dexvm threaded direct handlers cache checked to fast") {
    InterpreterConfig threaded_config;
    threaded_config.backend = InterpreterBackend::threaded;
    Vm cache_vm(threaded_config);
    const auto array_method = cache_vm.Static("LFlow;", "sumArray", "()I");
    const auto& array_code = cache_vm.linker.FastCodeFor(array_method);
    REQUIRE(array_code.instructions[1].handler ==
            FastHandler::object_checked);
    ExpectInt(cache_vm.interpreter.Call(array_method, {}), 24);
    CHECK(array_code.instructions[1].handler == FastHandler::object_fast);
    CHECK(array_code.instructions[1].resolved_id != kInvalidFastIndex);
    CHECK(array_code.instructions[1].resolved_aux != kInvalidFastIndex);
    ExpectInt(cache_vm.interpreter.Call(array_method, {}), 24);

    const auto field_method =
        cache_vm.Static("LClinitUser;", "read", "()I");
    const auto& field_code = cache_vm.linker.FastCodeFor(field_method);
    REQUIRE(field_code.instructions[0].handler ==
            FastHandler::object_checked);
    ExpectInt(cache_vm.interpreter.Call(field_method, {}), 55);
    CHECK(field_code.instructions[0].handler == FastHandler::object_fast);
    ExpectInt(cache_vm.interpreter.Call(field_method, {}), 55);

    const auto invoke_method =
        cache_vm.Static("LInvokeLoop;", "run", "(I)I");
    const auto& invoke_code = cache_vm.linker.FastCodeFor(invoke_method);
    const auto invoke_instruction = std::find_if(
        invoke_code.instructions.begin(), invoke_code.instructions.end(),
        [](const auto& candidate) {
            return candidate.handler == FastHandler::invoke_checked;
        });
    REQUIRE(invoke_instruction != invoke_code.instructions.end());
    ExpectInt(cache_vm.interpreter.Call(
                  invoke_method, std::vector<VmValue>{VmValue::Int(20)}),
              210);
    CHECK(invoke_instruction->handler == FastHandler::invoke_fast);
    REQUIRE(invoke_instruction->invoke < invoke_code.invokes.size());
    const auto& cached_invoke =
        invoke_code.invokes[invoke_instruction->invoke];
    CHECK(cached_invoke.resolved_method != kInvalidFastIndex);
    CHECK(cached_invoke.is_static);
    CHECK(cached_invoke.argument_shorty == std::vector<char>{'I'});
    ExpectInt(cache_vm.interpreter.Call(
                  invoke_method, std::vector<VmValue>{VmValue::Int(20)}),
              210);
    CHECK(cache_vm.interpreter.Stats().fast_code_builds > 0);
    CHECK(cache_vm.interpreter.Stats().fast_code_bytes > 0);
}

TEST_CASE("dexvm extra interpreter microbenchmark fixtures have fixed results") {
    WithEachBackend([](InterpreterConfig config) {
    Vm vm(config);
    ExpectInt(vm.CallStatic("LArrayLoop;", "run", "(I)I", {VmValue::Int(10)}),
              45);
    ExpectInt(vm.CallStatic("LSwitchLoop;", "run", "(I)I", {VmValue::Int(8)}),
              20);
    ExpectInt(vm.CallStatic("LInstanceLoop;", "run", "(I)I", {VmValue::Int(7)}),
              7);
    ExpectInt(vm.CallStatic("LVirtualLoop;", "run", "(I)I", {VmValue::Int(7)}),
              7);
    ExpectInt(vm.CallStatic("LWideLoop;", "run", "(I)I", {VmValue::Int(10)}),
              45);
    ExpectInt(vm.CallStatic("LWideArg;", "call", "()I"), 7);
    ExpectInt(vm.CallStatic("LWideArg;", "callRange", "()I"), 9);
    ExpectInt(vm.CallStatic("LTypeLoop;", "run", "(I)I", {VmValue::Int(9)}), 9);
    });
}

TEST_CASE("dexvm precheck rejects invoke and wide-field register words") {
    WithEachBackend([](InterpreterConfig config) {
        {
            Vm vm(config);
            InstallMalformedCode(vm, "LFlow;", "loopSum", "(I)I", 1,
                                 {0x1071U, 0x0000U, 0x0001U, 0x000eU});
            ExpectInvalidRegister(vm, "LFlow;", "loopSum", "(I)I",
                                  {VmValue::Int(1)});
        }
        {
            Vm vm(config);
            InstallMalformedCode(vm, "LFlow;", "loopSum", "(I)I", 2,
                                 {0x0277U, 0x0000U, 0x0001U, 0x000eU});
            ExpectInvalidRegister(vm, "LFlow;", "loopSum", "(I)I",
                                  {VmValue::Int(1)});
        }
        {
            Vm vm(config);
            InstallMalformedCode(vm, "LFlow;", "loopSum", "(I)I", 1,
                                 {0x1024U, 0x0000U, 0x0002U, 0x000eU});
            ExpectInvalidRegister(vm, "LFlow;", "loopSum", "(I)I",
                                  {VmValue::Int(1)});
        }
        {
            Vm vm(config);
            InstallMalformedCode(vm, "LFlow;", "loopSum", "(I)I", 2,
                                 {0x0153U, 0x0000U, 0x000eU});
            ExpectInvalidRegister(vm, "LFlow;", "loopSum", "(I)I",
                                  {VmValue::Int(1)});
        }
    });
}

TEST_CASE("dexvm invoke-wide 35c rejects non-consecutive and OOB pairs") {
    const auto compare_backends = [](auto&& patch, const char* needle) {
        const auto switch_error = CaptureInvokeWideError(
            InterpreterBackend::switch_dispatch, patch);
        const auto threaded_error =
            CaptureInvokeWideError(InterpreterBackend::threaded, patch);
        CHECK(switch_error.reason == DexVmErrorReason::invalid_register);
        CHECK(switch_error.what.find(needle) != std::string::npos);
        CHECK(threaded_error.reason == switch_error.reason);
        CHECK(threaded_error.what == switch_error.what);
    };
    compare_backends(
        [](LinkedMethod& method) {
            REQUIRE(PatchOpcodeRegisterWord(method.code->instructions, 0x71U,
                                            0x0020U));
        },
        "wide invoke argument is not a consecutive pair");
    compare_backends(
        [](LinkedMethod& method) {
            method.code->info.registers_size = 2;
            REQUIRE(PatchOpcodeRegisterWord(method.code->instructions, 0x71U,
                                            0x0011U));
        },
        "register out of range");
}

TEST_CASE("dexvm FastCode and Precheck share structural diagnostics") {
    ExpectMatchingStructuralDiagnostic({0x0114U},
                                       "instruction exceeds method end");
    ExpectMatchingStructuralDiagnostic({0x0228U, 0x000eU},
                                       "branch target out of method");
    ExpectMatchingStructuralDiagnostic({0x002bU, 0x0003U, 0x0000U, 0x000eU},
                                       "payload reference does not hit a payload");
    ExpectMatchingStructuralDiagnostic({0x00ffU}, "rejected opcode 255");
    ExpectMatchingStructuralDiagnostic({0x6071U, 0x0000U, 0x0000U, 0x000eU},
                                       "35c register count exceeds 5 at pc 0");
}

TEST_CASE("dexvm threaded straight microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_STRAIGHT_BENCH", "LFlow;", "loopSum",
                                 "(I)I", 200, 19'900);
}

TEST_CASE("dexvm threaded object microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_OBJECT_BENCH", "LFieldLoop;", "run",
                                 "(I)I", 200, 200);
}

TEST_CASE("dexvm threaded invoke microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_INVOKE_BENCH", "LInvokeLoop;", "run",
                                 "(I)I", 200, 20'100);
}

TEST_CASE("dexvm threaded array microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_ARRAY_BENCH", "LArrayLoop;", "run",
                                 "(I)I", 200, 19'900);
}

TEST_CASE("dexvm threaded switch microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_SWITCH_BENCH", "LSwitchLoop;", "run",
                                 "(I)I", 200, 500);
}

TEST_CASE("dexvm threaded instance microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_INSTANCE_BENCH", "LInstanceLoop;",
                                 "run", "(I)I", 200, 200);
}

TEST_CASE("dexvm threaded virtual microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_VIRTUAL_BENCH", "LVirtualLoop;", "run",
                                 "(I)I", 200, 200);
}

TEST_CASE("dexvm threaded wide microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_WIDE_BENCH", "LWideLoop;", "run",
                                 "(I)I", 200, 19'900);
}

TEST_CASE("dexvm threaded type microbenchmark reports switch comparison") {
    ReportThreadedMicrobenchmark("DEXVM_TYPE_BENCH", "LTypeLoop;", "run",
                                 "(I)I", 200, 200);
}
