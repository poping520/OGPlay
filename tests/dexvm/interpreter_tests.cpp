// DexVM stage-1 interpreter conformance over dexasm fixtures.
// Expected values are recorded with their semantic source: AOSP
// vm/mterp/c/OP_*.cpp at the pinned baseline (07 §2 mode B) or the Dalvik
// bytecode specification (docs/dalvik-bytecode at the same tag).

#include <doctest/doctest.h>

#include <algorithm>
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
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/object_model.h"
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

struct Vm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit Vm(const InterpreterConfig config = {},
                const JavaObjectModelConfig model_config = {},
                std::vector<IntrinsicClassDecl> extra_catalog = {})
        : model(strings, arrays, model_config),
          linker(),
          interpreter(
              [this, &extra_catalog]() -> DexClassLinker& {
                  auto catalog = CoreIntrinsicCatalog();
                  catalog.insert(
                      catalog.end(),
                      std::make_move_iterator(extra_catalog.begin()),
                      std::make_move_iterator(extra_catalog.end()));
                  linker.RegisterIntrinsics(catalog);
                  linker.RegisterDex(ReadFixture("interp.dex"));
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger, config) {}

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

    [[nodiscard]] VmCallOutcome CallStatic(
        const std::string& class_descriptor, const std::string& name,
        const std::string& descriptor, std::vector<VmValue> arguments = {}) {
        return interpreter.Call(Static(class_descriptor, name, descriptor),
                                arguments);
    }
};

struct IntrinsicVm final {
    JniStringStore strings;
    JniPrimitiveArrayStore arrays;
    JavaObjectModel model;
    DexClassLinker linker;
    ogplay::core::CapabilityLedger ledger;
    Interpreter interpreter;

    explicit IntrinsicVm(std::vector<IntrinsicClassDecl> catalog)
        : model(strings, arrays),
          linker(),
          interpreter(
              [this, &catalog]() -> DexClassLinker& {
                  auto core = CoreIntrinsicCatalog();
                  core.insert(core.end(),
                              std::make_move_iterator(catalog.begin()),
                              std::make_move_iterator(catalog.end()));
                  linker.RegisterIntrinsics(core);
                  linker.Link();
                  return linker;
              }(),
              model, nullptr, ledger) {}

    [[nodiscard]] VmMethodId Static(const std::string& class_descriptor,
                                    const std::string& name,
                                    const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE(java_class.has_value());
        const auto method =
            linker.FindDirectMethod(*java_class, name, descriptor);
        REQUIRE(method.has_value());
        return *method;
    }

    [[nodiscard]] VmMethodId Virtual(const std::string& class_descriptor,
                                     const std::string& name,
                                     const std::string& descriptor) {
        const auto java_class = linker.FindClass(class_descriptor);
        REQUIRE(java_class.has_value());
        for (const auto method : linker.Class(*java_class).vtable) {
            const auto& linked = linker.Method(method);
            if (linked.name == name && linked.descriptor == descriptor) {
                return method;
            }
        }
        FAIL("virtual method is missing: ", class_descriptor, ".", name,
             descriptor);
        return VmMethodId{};
    }
};

TEST_CASE("DexVM Java identity is independent from handles and catalog order") {
    const auto call_one = [](IntrinsicVm& vm, const VmMethodId method,
                             const VmValue argument) {
        return vm.interpreter.Call(method,
                                   std::vector<VmValue>{argument});
    };
    const auto expect_int = [](const VmCallOutcome& outcome,
                               const std::int32_t expected) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
        CHECK(outcome.value.AsInt() == expected);
    };
    const auto make_catalog = [](const bool reverse) {
        auto first = IntrinsicClassBuilder::Class("Lidentity/First;").Build();
        auto second =
            IntrinsicClassBuilder::Class("Lidentity/Second;").Build();
        std::vector<IntrinsicClassDecl> catalog;
        if (reverse) {
            catalog.push_back(std::move(second));
            catalog.push_back(std::move(first));
        } else {
            catalog.push_back(std::move(first));
            catalog.push_back(std::move(second));
        }
        return catalog;
    };

    IntrinsicVm left(make_catalog(false));
    IntrinsicVm right(make_catalog(true));
    const auto left_first = left.linker.ResolveDescriptor("Lidentity/First;");
    const auto left_second =
        left.linker.ResolveDescriptor("Lidentity/Second;");
    const auto right_first =
        right.linker.ResolveDescriptor("Lidentity/First;");
    const auto right_second =
        right.linker.ResolveDescriptor("Lidentity/Second;");
    CHECK(left_first != right_first);

    const auto left_class = left.model.ClassObject(left_first);
    static_cast<void>(left_second);
    static_cast<void>(right.model.ClassObject(right_second));
    const auto right_class = right.model.ClassObject(right_first);
    CHECK(left_class != right_class);
    CHECK(left.model.IdentityHashCode(left_class) ==
          right.model.IdentityHashCode(right_class));

    const auto left_object =
        left.interpreter.NewIntrinsicInstance("Lidentity/First;");
    const auto right_object =
        right.interpreter.NewIntrinsicInstance("Lidentity/First;");
    CHECK(left_object != right_object);
    const auto identity_hash = left.model.IdentityHashCode(left_object);
    CHECK(identity_hash == right.model.IdentityHashCode(right_object));

    const auto object_hash = call_one(
        left,
        left.Virtual("Lidentity/First;", "hashCode", "()I"),
        VmValue::Ref(left_object));
    expect_int(object_hash, identity_hash);
    const auto system_hash = call_one(
        left,
        left.Static("Ljava/lang/System;", "identityHashCode",
                    "(Ljava/lang/Object;)I"),
        VmValue::Ref(left_object));
    expect_int(system_hash, identity_hash);
    expect_int(call_one(left,
                        left.Static("Ljava/lang/System;", "identityHashCode",
                                    "(Ljava/lang/Object;)I"),
                        VmValue::Ref(VmObjectRef{})),
               0);

    const auto rendered = call_one(
        left,
        left.Virtual("Lidentity/First;", "toString",
                     "()Ljava/lang/String;"),
        VmValue::Ref(left_object));
    REQUIRE_FALSE(rendered.exception.IsValid());
    const auto text = left.interpreter.StringUtf8(rendered.value.ref);
    REQUIRE(text.starts_with("identity.First@"));
    CHECK(static_cast<std::uint32_t>(std::stoul(text.substr(15), nullptr, 16)) ==
          static_cast<std::uint32_t>(identity_hash));

    auto override = IntrinsicClassBuilder::Class("Lidentity/Override;");
    override.VirtualMethod("hashCode", "()I", [](IntrinsicContext&) {
        return VmValue::Int(777);
    });
    const auto existing_throwable = std::make_shared<VmObjectRef>();
    auto throwing = IntrinsicClassBuilder::Class("Lidentity/Throwing;");
    throwing.VirtualMethod(
        "hashCode", "()I",
        [existing_throwable](IntrinsicContext& context) {
            context.vm.SetPendingException(*existing_throwable);
            return VmValue::Int(0);
        });
    IntrinsicVm overridden(
        {std::move(override).Build(), std::move(throwing).Build()});
    const auto overridden_object =
        overridden.interpreter.NewIntrinsicInstance("Lidentity/Override;");
    const auto overridden_identity_hash =
        overridden.model.IdentityHashCode(overridden_object);
    expect_int(call_one(overridden,
                        overridden.Virtual("Lidentity/Override;", "hashCode",
                                           "()I"),
                        VmValue::Ref(overridden_object)),
               777);
    expect_int(call_one(overridden,
                        overridden.Static("Ljava/lang/System;",
                                          "identityHashCode",
                                          "(Ljava/lang/Object;)I"),
                        VmValue::Ref(overridden_object)),
               overridden_identity_hash);
    const auto overridden_rendered = call_one(
        overridden,
        overridden.Virtual("Lidentity/Override;", "toString",
                           "()Ljava/lang/String;"),
        VmValue::Ref(overridden_object));
    REQUIRE_FALSE(overridden_rendered.exception.IsValid());
    CHECK(overridden.interpreter.StringUtf8(overridden_rendered.value.ref) ==
          "identity.Override@309");

    *existing_throwable = overridden.interpreter.MakeThrowable(
        "Ljava/lang/IllegalStateException;", "existing throwable");
    const auto throwing_object =
        overridden.interpreter.NewIntrinsicInstance("Lidentity/Throwing;");
    const auto throwing_rendered = call_one(
        overridden,
        overridden.Virtual("Lidentity/Throwing;", "toString",
                           "()Ljava/lang/String;"),
        VmValue::Ref(throwing_object));
    CHECK(throwing_rendered.exception == *existing_throwable);
    CHECK(throwing_rendered.exception_stack.empty());

    const auto string_class =
        overridden.linker.ResolveDescriptor("Ljava/lang/String;");
    const auto unbound_string = overridden.interpreter.NewIntrinsicInstance(
        "Ljava/lang/String;");
    CHECK(overridden.model.ObjectClass(unbound_string) == string_class);
    const auto string_hash_before =
        overridden.model.IdentityHashCode(unbound_string);
    const auto string_identity_before =
        overridden.model.ToIdentity(unbound_string);
    overridden.model.BindString(unbound_string, u"bound");
    const auto string_identity_after =
        overridden.model.ToIdentity(unbound_string);
    CHECK(overridden.model.IdentityHashCode(unbound_string) ==
          string_hash_before);
    CHECK(string_identity_after != string_identity_before);
    CHECK(overridden.model.FindIdentity(string_identity_after) ==
          unbound_string);
    CHECK(overridden.model.Kind(unbound_string) == VmObjectKind::string);
}

void ExpectInt(const VmCallOutcome& outcome, const std::int32_t expected) {
    REQUIRE_MESSAGE(!outcome.exception.IsValid(),
                    "unexpected exception: ", outcome.exception_message);
    REQUIRE(outcome.value.kind == VmValue::Kind::cat1);
    CHECK(outcome.value.AsInt() == expected);
}

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

template <typename Fn>
void WithEachBackend(Fn&& fn) {
    for (const auto backend : {InterpreterBackend::switch_dispatch,
                               InterpreterBackend::threaded}) {
        INFO("backend=",
             backend == InterpreterBackend::threaded ? "threaded" : "switch");
        InterpreterConfig config;
        config.backend = backend;
        fn(config);
    }
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

template <typename VmType>
void ExpectException(const VmType& vm, const VmCallOutcome& outcome,
                     const std::string& descriptor) {
    REQUIRE(outcome.exception.IsValid());
    CHECK(vm.linker.Class(outcome.exception_class).descriptor == descriptor);
}

}  // namespace

TEST_CASE("dexvm core intrinsic catalog is unique and structurally stable") {
    const auto catalog = CoreIntrinsicCatalog();
    std::set<std::string> descriptors;
    const std::set<std::string> intentionally_unimplemented = {
        "Ljava/lang/System;.currentTimeMillis()J",
        "Ljava/lang/System;.nanoTime()J",
        "Ljava/lang/System;.load(Ljava/lang/String;)V",
        "Ljava/lang/System;.loadLibrary(Ljava/lang/String;)V",
        "Ljava/lang/System;.exit(I)V",
        "Ljava/util/Date;.<init>()V",
        "Ljava/util/Date;.getTime()J",
        "Ljava/util/Date;.getYear()I",
        "Ljava/lang/AssertionError;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/AssertionError;.<init>(Ljava/lang/Object;)V",
        "Ljava/lang/AssertionError;.<init>(Z)V",
        "Ljava/lang/AssertionError;.<init>(C)V",
        "Ljava/lang/AssertionError;.<init>(I)V",
        "Ljava/lang/AssertionError;.<init>(J)V",
        "Ljava/lang/AssertionError;.<init>(F)V",
        "Ljava/lang/AssertionError;.<init>(D)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/Throwable;)V",
        "Ljava/lang/ReflectiveOperationException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/SecurityException;.<init>(Ljava/lang/Throwable;)V",
        "Ljava/lang/TypeNotPresentException;.<init>"
        "(Ljava/lang/String;Ljava/lang/Throwable;)V",
        "Ljava/lang/Appendable;.append(C)Ljava/lang/Appendable;",
        "Ljava/lang/Appendable;.append"
        "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;",
        "Ljava/lang/Appendable;.append"
        "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;",
        "Ljava/lang/AutoCloseable;.close()V",
        "Ljava/lang/CharSequence;.charAt(I)C",
        "Ljava/lang/CharSequence;.subSequence(II)Ljava/lang/CharSequence;",
        "Ljava/lang/CharSequence;.toString()Ljava/lang/String;",
        "Ljava/lang/Comparable;.compareTo(Ljava/lang/Object;)I",
        "Ljava/lang/Iterable;.iterator()Ljava/util/Iterator;",
        "Ljava/lang/Readable;.read(Ljava/nio/CharBuffer;)I",
        "Ljava/lang/Runnable;.run()V",
        "Ljava/lang/Thread;.stop()V",
        "Ljava/lang/Thread;.suspend()V",
        "Ljava/lang/Thread;.resume()V",
        "Ljava/lang/Thread;.destroy()V",
        "Ljava/lang/BootClassLoader;.<init>()V",
        "Ldalvik/system/PathClassLoader;.<init>"
        "(Ljava/lang/String;Ljava/lang/ClassLoader;)V",
        "Ldalvik/system/PathClassLoader;.<init>"
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V",
        "Ljava/lang/reflect/AnnotatedElement;.isAnnotationPresent"
        "(Ljava/lang/Class;)Z",
        "Ljava/lang/reflect/AnnotatedElement;.getAnnotation"
        "(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;",
        "Ljava/lang/reflect/AnnotatedElement;.getAnnotations"
        "()[Ljava/lang/annotation/Annotation;",
        "Ljava/lang/reflect/AnnotatedElement;.getDeclaredAnnotations"
        "()[Ljava/lang/annotation/Annotation;",
        "Ljava/lang/reflect/GenericDeclaration;.getTypeParameters"
        "()[Ljava/lang/reflect/TypeVariable;",
        "Ljava/lang/reflect/Member;.getDeclaringClass()Ljava/lang/Class;",
        "Ljava/lang/reflect/Member;.getName()Ljava/lang/String;",
        "Ljava/lang/reflect/Member;.getModifiers()I",
        "Ljava/lang/reflect/Member;.isSynthetic()Z",
    };
    for (const auto& declaration : catalog) {
        CHECK(descriptors.insert(declaration.descriptor).second);
        for (const auto& method : declaration.methods) {
            if (!method.implementation) {
                CHECK(intentionally_unimplemented.contains(
                    declaration.descriptor + "." + method.name +
                    method.descriptor));
            }
        }
    }

    const auto signatures = [&catalog](const std::string& descriptor) {
        std::set<std::pair<std::string, std::string>> result;
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        for (const auto& method : declaration->methods) {
            result.emplace(method.name, method.descriptor);
        }
        return result;
    };

    CHECK(signatures("Ljava/lang/Object;") ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"clone", "()Ljava/lang/Object;"},
              {"equals", "(Ljava/lang/Object;)Z"},
              {"getClass", "()Ljava/lang/Class;"},
              {"hashCode", "()I"},
              {"notify", "()V"},
              {"notifyAll", "()V"},
              {"toString", "()Ljava/lang/String;"},
              {"wait", "()V"},
              {"wait", "(J)V"},
              {"wait", "(JI)V"},
          });
    CHECK(signatures("Ljava/lang/Cloneable;").empty());
    CHECK(signatures("Ljava/lang/Appendable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"append", "(C)Ljava/lang/Appendable;"},
              {"append", "(Ljava/lang/CharSequence;)Ljava/lang/Appendable;"},
              {"append",
               "(Ljava/lang/CharSequence;II)Ljava/lang/Appendable;"},
          });
    CHECK(signatures("Ljava/lang/AutoCloseable;") ==
          std::set<std::pair<std::string, std::string>>{{"close", "()V"}});
    CHECK(signatures("Ljava/lang/Iterable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"iterator", "()Ljava/util/Iterator;"},
          });
    CHECK(signatures("Ljava/lang/Readable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"read", "(Ljava/nio/CharBuffer;)I"},
          });
    CHECK(signatures("Ljava/lang/CharSequence;") ==
          std::set<std::pair<std::string, std::string>>{
              {"length", "()I"},
              {"charAt", "(I)C"},
              {"subSequence", "(II)Ljava/lang/CharSequence;"},
              {"toString", "()Ljava/lang/String;"},
          });
    CHECK(signatures("Ljava/lang/Comparable;") ==
          std::set<std::pair<std::string, std::string>>{
              {"compareTo", "(Ljava/lang/Object;)I"},
          });
    CHECK(signatures("Ljava/lang/Runnable;") ==
          std::set<std::pair<std::string, std::string>>{{"run", "()V"}});
    CHECK(signatures("Ljava/lang/StringBuilder;").size() == 17U);
    CHECK(signatures("Ljava/lang/String;").size() == 43U);
    CHECK(signatures("Ljava/lang/Integer;").size() == 37U);
}

TEST_CASE("dormant classes with missing hierarchy link only when reached") {
    Vm vm;

    // An unrelated usable class proves Link() no longer rejects the entire
    // DEX because an optional packaged class names an absent framework base.
    CHECK(vm.linker.ResolveDescriptor("LCounter;").IsValid());

    try {
        static_cast<void>(
            vm.linker.ResolveDescriptor("LDormantOptional;"));
        FAIL("reached class with missing hierarchy did not fail");
    } catch (const DexVmError& error) {
        const std::string message = error.what();
        CHECK(message.find("class hierarchy is not available: ") !=
              std::string::npos);
        CHECK(message.find("Landroid/optional/MissingActivity;") !=
              std::string::npos);
        CHECK(message.find("required by LDormantOptional;") !=
              std::string::npos);
    }
}

TEST_CASE("survey records a missing hierarchy only when its class is reached") {
    Vm vm;
    vm.linker.EnableGapSurvey();
    CHECK(vm.linker.GapSurveyHits().empty());

    const auto optional =
        vm.linker.ResolveDescriptor("LDormantOptional;");
    CHECK(optional.IsValid());
    const auto hits = vm.linker.GapSurveyHits();
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].owner_descriptor ==
          "Landroid/optional/MissingActivity;");
    CHECK(hits[0].member.empty());
}

TEST_CASE("dexvm API 19 primitive wrapper family inventory is complete") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto declaration = [&catalog](const std::string& descriptor)
        -> const IntrinsicClassDecl& {
        const auto found = std::find_if(catalog.begin(), catalog.end(),
            [&](const auto& candidate) { return candidate.descriptor == descriptor; });
        REQUIRE(found != catalog.end());
        CHECK(std::count_if(catalog.begin(), catalog.end(), [&](const auto& candidate) {
            return candidate.descriptor == descriptor; }) == 1);
        return *found;
    };
    const auto signatures = [](const IntrinsicClassDecl& java_class) {
        std::set<std::string> result;
        for (const auto& method : java_class.methods) {
            result.insert(method.name + method.descriptor);
            CHECK(static_cast<bool>(method.implementation));
        }
        return result;
    };
    const auto fields = [](const IntrinsicClassDecl& java_class) {
        std::set<std::string> result;
        for (const auto& field : java_class.fields) {
            result.insert(field.name + ":" + field.descriptor);
        }
        return result;
    };
    const auto expect = [&](const char* name, const char* superclass,
                            std::initializer_list<const char*> interfaces,
                            std::initializer_list<const char*> methods) {
        const auto& java_class = declaration("Ljava/lang/" + std::string(name) + ";");
        REQUIRE(java_class.superclass.has_value());
        CHECK(*java_class.superclass == superclass);
        CHECK(std::set<std::string>(java_class.interfaces.begin(), java_class.interfaces.end()) ==
              std::set<std::string>(interfaces.begin(), interfaces.end()));
        CHECK(signatures(java_class) == std::set<std::string>(methods.begin(), methods.end()));
    };

    expect("Number", "Ljava/lang/Object;", {"Ljava/io/Serializable;"}, {
        "<init>()V", "byteValue()B", "shortValue()S", "intValue()I",
        "longValue()J", "floatValue()F", "doubleValue()D"});
    expect("Byte", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(B)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compareTo(Ljava/lang/Byte;)I","compare(BB)I","decode(Ljava/lang/String;)Ljava/lang/Byte;","equals(Ljava/lang/Object;)Z","hashCode()I",
        "parseByte(Ljava/lang/String;)B","parseByte(Ljava/lang/String;I)B","toString()Ljava/lang/String;","toString(B)Ljava/lang/String;","toHexString(BZ)Ljava/lang/String;",
        "valueOf(B)Ljava/lang/Byte;","valueOf(Ljava/lang/String;)Ljava/lang/Byte;","valueOf(Ljava/lang/String;I)Ljava/lang/Byte;"});
    expect("Short", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(S)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compareTo(Ljava/lang/Short;)I","compare(SS)I","decode(Ljava/lang/String;)Ljava/lang/Short;","equals(Ljava/lang/Object;)Z","hashCode()I",
        "parseShort(Ljava/lang/String;)S","parseShort(Ljava/lang/String;I)S","toString()Ljava/lang/String;","toString(S)Ljava/lang/String;","reverseBytes(S)S",
        "valueOf(S)Ljava/lang/Short;","valueOf(Ljava/lang/String;)Ljava/lang/Short;","valueOf(Ljava/lang/String;I)Ljava/lang/Short;"});
    const auto integral_methods = [](const char* wrapper, const char* primitive,
                                     const char* parse, const char* property) {
        std::set<std::string> out{
            std::string("<init>(")+primitive+")V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
            std::string("compareTo(Ljava/lang/")+wrapper+";)I",std::string("compare(")+primitive+primitive+")I",std::string("decode(Ljava/lang/String;)Ljava/lang/")+wrapper+";",
            "equals(Ljava/lang/Object;)Z","hashCode()I",std::string(parse)+"(Ljava/lang/String;)"+primitive,std::string(parse)+"(Ljava/lang/String;I)"+primitive,
            "toString()Ljava/lang/String;",std::string("toString(")+primitive+")Ljava/lang/String;",std::string("toString(")+primitive+"I)Ljava/lang/String;",
            std::string("toBinaryString(")+primitive+")Ljava/lang/String;",std::string("toHexString(")+primitive+")Ljava/lang/String;",std::string("toOctalString(")+primitive+")Ljava/lang/String;",
            std::string("valueOf(")+primitive+")Ljava/lang/"+wrapper+";",std::string("valueOf(Ljava/lang/String;)Ljava/lang/")+wrapper+";",std::string("valueOf(Ljava/lang/String;I)Ljava/lang/")+wrapper+";",
            std::string(property)+"(Ljava/lang/String;)Ljava/lang/"+wrapper+";",std::string(property)+"(Ljava/lang/String;"+primitive+")Ljava/lang/"+wrapper+";",std::string(property)+"(Ljava/lang/String;Ljava/lang/"+wrapper+";)Ljava/lang/"+wrapper+";",
            std::string("highestOneBit(")+primitive+")"+primitive,std::string("lowestOneBit(")+primitive+")"+primitive,std::string("numberOfLeadingZeros(")+primitive+")I",std::string("numberOfTrailingZeros(")+primitive+")I",
            std::string("bitCount(")+primitive+")I",std::string("rotateLeft(")+primitive+"I)"+primitive,std::string("rotateRight(")+primitive+"I)"+primitive,
            std::string("reverseBytes(")+primitive+")"+primitive,std::string("reverse(")+primitive+")"+primitive,std::string("signum(")+primitive+")I"};
        return out;
    };
    const auto check_integral = [&](const char* wrapper, const char* primitive,
                                    const char* parse, const char* property) {
        const auto actual = signatures(declaration(
            "Ljava/lang/" + std::string(wrapper) + ";"));
        const auto expected = integral_methods(wrapper, primitive, parse, property);
        std::vector<std::string> missing;
        std::vector<std::string> extra;
        std::set_difference(expected.begin(), expected.end(), actual.begin(),
                            actual.end(), std::back_inserter(missing));
        std::set_difference(actual.begin(), actual.end(), expected.begin(),
                            expected.end(), std::back_inserter(extra));
        std::string missing_text;
        std::string extra_text;
        for (const auto& value : missing) missing_text += value + " | ";
        for (const auto& value : extra) extra_text += value + " | ";
        CAPTURE(std::string(wrapper));
        CAPTURE(missing_text);
        CAPTURE(extra_text);
        CHECK(actual == expected);
    };
    check_integral("Integer", "I", "parseInt", "getInteger");
    check_integral("Long", "J", "parseLong", "getLong");
    expect("Boolean", "Ljava/lang/Object;", {"Ljava/io/Serializable;","Ljava/lang/Comparable;"}, {
        "<init>(Z)V","<init>(Ljava/lang/String;)V","booleanValue()Z","compare(ZZ)I","compareTo(Ljava/lang/Boolean;)I","equals(Ljava/lang/Object;)Z","hashCode()I",
        "getBoolean(Ljava/lang/String;)Z","parseBoolean(Ljava/lang/String;)Z","toString()Ljava/lang/String;","toString(Z)Ljava/lang/String;","valueOf(Z)Ljava/lang/Boolean;","valueOf(Ljava/lang/String;)Ljava/lang/Boolean;"});
    expect("Float", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(F)V","<init>(D)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compare(FF)I","compareTo(Ljava/lang/Float;)I","equals(Ljava/lang/Object;)Z","hashCode()I","isInfinite()Z","isInfinite(F)Z","isNaN()Z","isNaN(F)Z",
        "parseFloat(Ljava/lang/String;)F","toString()Ljava/lang/String;","toString(F)Ljava/lang/String;","toHexString(F)Ljava/lang/String;","valueOf(F)Ljava/lang/Float;","valueOf(Ljava/lang/String;)Ljava/lang/Float;",
        "floatToIntBits(F)I","floatToRawIntBits(F)I","intBitsToFloat(I)F"});
    expect("Double", "Ljava/lang/Number;", {"Ljava/lang/Comparable;"}, {
        "<init>(D)V","<init>(Ljava/lang/String;)V","byteValue()B","shortValue()S","intValue()I","longValue()J","floatValue()F","doubleValue()D",
        "compare(DD)I","compareTo(Ljava/lang/Double;)I","equals(Ljava/lang/Object;)Z","hashCode()I","isInfinite()Z","isInfinite(D)Z","isNaN()Z","isNaN(D)Z",
        "parseDouble(Ljava/lang/String;)D","toString()Ljava/lang/String;","toString(D)Ljava/lang/String;","toHexString(D)Ljava/lang/String;","valueOf(D)Ljava/lang/Double;","valueOf(Ljava/lang/String;)Ljava/lang/Double;",
        "doubleToLongBits(D)J","doubleToRawLongBits(D)J","longBitsToDouble(J)D"});

    CHECK(fields(declaration("Ljava/lang/Number;")).empty());
    CHECK(fields(declaration("Ljava/lang/Byte;")) == std::set<std::string>{
        "MAX_VALUE:B","MIN_VALUE:B","SIZE:I","TYPE:Ljava/lang/Class;","value:B"});
    CHECK(fields(declaration("Ljava/lang/Short;")) == std::set<std::string>{
        "MAX_VALUE:S","MIN_VALUE:S","SIZE:I","TYPE:Ljava/lang/Class;","value:S"});
    CHECK(fields(declaration("Ljava/lang/Integer;")) == std::set<std::string>{
        "MAX_VALUE:I","MIN_VALUE:I","SIZE:I","TYPE:Ljava/lang/Class;","value:I"});
    CHECK(fields(declaration("Ljava/lang/Long;")) == std::set<std::string>{
        "MAX_VALUE:J","MIN_VALUE:J","SIZE:I","TYPE:Ljava/lang/Class;","value:J"});
    const std::set<std::string> floating_fields{
        "MAX_EXPONENT:I","MAX_VALUE:X","MIN_EXPONENT:I","MIN_NORMAL:X",
        "MIN_VALUE:X","NEGATIVE_INFINITY:X","NaN:X","POSITIVE_INFINITY:X",
        "SIZE:I","TYPE:Ljava/lang/Class;","value:X"};
    const auto specialize_floating_fields = [&](const char primitive) {
        auto result = floating_fields;
        std::set<std::string> specialized;
        for (auto field : result) {
            if (field.ends_with(":X")) field.back() = primitive;
            specialized.insert(std::move(field));
        }
        return specialized;
    };
    CHECK(fields(declaration("Ljava/lang/Float;")) == specialize_floating_fields('F'));
    CHECK(fields(declaration("Ljava/lang/Double;")) == specialize_floating_fields('D'));
    CHECK(fields(declaration("Ljava/lang/Boolean;")) == std::set<std::string>{
        "FALSE:Ljava/lang/Boolean;","TRUE:Ljava/lang/Boolean;",
        "TYPE:Ljava/lang/Class;","value:Z"});

    const auto& character = declaration("Ljava/lang/Character;");
    CHECK(*character.superclass == "Ljava/lang/Object;");
    CHECK(character.interfaces.size() == 2U);
    CHECK(signatures(character) == std::set<std::string>{
        "<init>(C)V","charValue()C","valueOf(C)Ljava/lang/Character;",
        "compareTo(Ljava/lang/Character;)I","compare(CC)I","equals(Ljava/lang/Object;)Z","hashCode()I",
        "toString()Ljava/lang/String;","toString(C)Ljava/lang/String;","digit(CI)I","digit(II)I","forDigit(II)C",
        "isDigit(C)Z","isDigit(I)Z","isLetter(C)Z","isLetter(I)Z","isLetterOrDigit(C)Z","isLetterOrDigit(I)Z",
        "isLowerCase(C)Z","isLowerCase(I)Z","isUpperCase(C)Z","isUpperCase(I)Z","isWhitespace(C)Z","isWhitespace(I)Z",
        "isSpaceChar(C)Z","isSpaceChar(I)Z","isSpace(C)Z","isISOControl(C)Z","isISOControl(I)Z",
        "toLowerCase(C)C","toLowerCase(I)I","toUpperCase(C)C","toUpperCase(I)I",
        "isHighSurrogate(C)Z","isLowSurrogate(C)Z","isSurrogatePair(CC)Z","isValidCodePoint(I)Z","isBmpCodePoint(I)Z",
        "isSupplementaryCodePoint(I)Z","charCount(I)I","toCodePoint(CC)I","highSurrogate(I)C","lowSurrogate(I)C","reverseBytes(C)C"});
    CHECK(fields(character).size() == 66U);
}

TEST_CASE("dexvm primitive wrapper parsing bits and Character boundaries") {
    Vm vm;
    const auto string = [&](std::string_view value) {
        return VmValue::Ref(vm.interpreter.NewStringUtf8(value));
    };
    const auto call_on = [&](const char* owner, VmObjectRef receiver,
                             const char* name, const char* descriptor,
                             std::vector<VmValue> arguments = {}) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        std::optional<VmMethodId> method;
        for (const auto candidate : vm.linker.Class(*java_class).vtable) {
            if (!candidate.IsValid()) continue;
            const auto& linked = vm.linker.Method(candidate);
            if (linked.name == name && linked.descriptor == descriptor) {
                method = candidate;
                break;
            }
        }
        CAPTURE(std::string(owner));
        CAPTURE(std::string(name));
        CAPTURE(std::string(descriptor));
        REQUIRE(method.has_value());
        arguments.insert(arguments.begin(), VmValue::Ref(receiver));
        return vm.interpreter.Call(*method, arguments);
    };
    const auto as_string = [&](const VmCallOutcome& outcome) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        return vm.interpreter.StringUtf8(outcome.value.ref);
    };
    const auto expect_long = [](const VmCallOutcome& outcome,
                                std::int64_t expected) {
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.kind == VmValue::Kind::wide);
        CHECK(outcome.value.AsLong() == expected);
    };
    const auto static_bits = [&](const char* owner, const char* name,
                                 const char* descriptor) {
        const auto java_class = vm.linker.FindClass(owner);
        REQUIRE(java_class.has_value());
        const auto field = vm.linker.FindFieldRecursive(
            *java_class, name, descriptor);
        REQUIRE(field.has_value());
        const auto& linked = vm.linker.Field(*field);
        const auto& storage = vm.linker.Class(linked.owner).static_storage;
        std::uint64_t bits = storage[linked.slot];
        if (linked.is_wide) bits |= static_cast<std::uint64_t>(
            storage[linked.slot + 1]) << 32U;
        return bits;
    };
    const auto parse_radix = vm.Static("Ljava/lang/Integer;", "parseInt",
                                       "(Ljava/lang/String;I)I");
    REQUIRE(parse_radix.Value() < 10000U);
    ExpectInt(vm.interpreter.Call(
                  parse_radix, std::vector<VmValue>{string("7fffffff"),
                                                    VmValue::Int(16)}),
              std::numeric_limits<std::int32_t>::max());
    ExpectException(vm, vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {string("2147483648")}), "Ljava/lang/NumberFormatException;");
    const auto parsed_min = vm.CallStatic("Ljava/lang/Long;", "parseLong", "(Ljava/lang/String;)J", {string("-9223372036854775808")});
    REQUIRE_FALSE(parsed_min.exception.IsValid());
    CHECK(parsed_min.value.AsLong() == std::numeric_limits<std::int64_t>::min());
    ExpectException(vm, vm.CallStatic("Ljava/lang/Byte;", "parseByte", "(Ljava/lang/String;)B", {string("128")}), "Ljava/lang/NumberFormatException;");
    ExpectInt(vm.CallStatic("Ljava/lang/Short;", "reverseBytes", "(S)S", {VmValue::Int(0x0100)}), 1);
    ExpectException(vm, vm.CallStatic("Ljava/lang/Integer;", "parseInt", "(Ljava/lang/String;)I", {VmValue::Ref(VmObjectRef{0})}), "Ljava/lang/NumberFormatException;");
    ExpectException(vm, vm.CallStatic("Ljava/lang/Float;", "parseFloat",
                                      "(Ljava/lang/String;)F",
                                      {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic("Ljava/lang/Double;", "parseDouble",
                                      "(Ljava/lang/String;)D",
                                      {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Float;", "valueOf",
                            "(Ljava/lang/String;)Ljava/lang/Float;",
                            {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Double;", "valueOf",
                            "(Ljava/lang/String;)Ljava/lang/Double;",
                            {VmValue::Ref(VmObjectRef{})}),
                    "Ljava/lang/NullPointerException;");

    const std::string tiny_float_text =
        "0." + std::string(80, '0') + "1";
    const auto tiny_float = vm.CallStatic(
        "Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F",
        {string(tiny_float_text)});
    REQUIRE_FALSE(tiny_float.exception.IsValid());
    CHECK(tiny_float.value.AsFloat() == 0.0F);
    CHECK_FALSE(std::signbit(tiny_float.value.AsFloat()));

    const std::string tiny_double_text =
        "-0." + std::string(400, '0') + "1";
    const auto tiny_double = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string(tiny_double_text)});
    REQUIRE_FALSE(tiny_double.exception.IsValid());
    CHECK(tiny_double.value.AsDouble() == 0.0);
    CHECK(std::signbit(tiny_double.value.AsDouble()));

    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Float;", "parseFloat",
                            "(Ljava/lang/String;)F", {string("0x1.0")}),
                    "Ljava/lang/NumberFormatException;");
    ExpectException(vm, vm.CallStatic(
                            "Ljava/lang/Double;", "parseDouble",
                            "(Ljava/lang/String;)D", {string("-0X1.0")}),
                    "Ljava/lang/NumberFormatException;");

    const auto hex_float = vm.CallStatic(
        "Ljava/lang/Float;", "parseFloat", "(Ljava/lang/String;)F",
        {string("0x1.0p0")});
    REQUIRE_FALSE(hex_float.exception.IsValid());
    CHECK(hex_float.value.AsFloat() == 1.0F);
    const auto hex_double = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string("-0x1.8p1")});
    REQUIRE_FALSE(hex_double.exception.IsValid());
    CHECK(hex_double.value.AsDouble() == -3.0);

    const std::string huge_negative_exponent =
        std::string(400, '9') + "e-1";
    const auto still_overflow = vm.CallStatic(
        "Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D",
        {string(huge_negative_exponent)});
    REQUIRE_FALSE(still_overflow.exception.IsValid());
    CHECK(std::isinf(still_overflow.value.AsDouble()));
    CHECK(still_overflow.value.AsDouble() > 0.0);

    const auto decoded = vm.CallStatic("Ljava/lang/Integer;", "decode", "(Ljava/lang/String;)Ljava/lang/Integer;", {string("-0x80000000")});
    REQUIRE_FALSE(decoded.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Integer;", decoded.value.ref, "intValue", "()I"), std::numeric_limits<std::int32_t>::min());
    CHECK(as_string(vm.CallStatic("Ljava/lang/Integer;", "toString", "(II)Ljava/lang/String;", {VmValue::Int(35), VmValue::Int(36)})) == "z");
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(-1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(0)}), 32);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(0)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "highestOneBit", "(I)I", {VmValue::Int(-1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "lowestOneBit", "(I)I", {VmValue::Int(std::numeric_limits<std::int32_t>::max())}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(1)}), 31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfLeadingZeros", "(I)I", {VmValue::Int(-1)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "numberOfTrailingZeros", "(I)I", {VmValue::Int(std::numeric_limits<std::int32_t>::min())}), 31);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "rotateLeft", "(II)I", {VmValue::Int(1), VmValue::Int(33)}), 2);
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverse", "(I)I", {VmValue::Int(1)}), std::numeric_limits<std::int32_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Integer;", "reverseBytes", "(I)I", {VmValue::Int(0x01020304)}), 0x04030201);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(0)}), 0);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "highestOneBit", "(J)J", {VmValue::Long(-1)}), std::numeric_limits<std::int64_t>::min());
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfLeadingZeros", "(J)I", {VmValue::Long(1)}), 63);
    ExpectInt(vm.CallStatic("Ljava/lang/Long;", "numberOfTrailingZeros", "(J)I", {VmValue::Long(std::numeric_limits<std::int64_t>::min())}), 63);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateLeft", "(JI)J", {VmValue::Long(1), VmValue::Int(65)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "rotateRight", "(JI)J", {VmValue::Long(1), VmValue::Int(-1)}), 2);
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverse", "(J)J", {VmValue::Long(1)}), std::numeric_limits<std::int64_t>::min());
    expect_long(vm.CallStatic("Ljava/lang/Long;", "reverseBytes", "(J)J", {VmValue::Long(0x0102030405060708LL)}), 0x0807060504030201LL);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "compare", "(FF)I", {VmValue::Float(-0.0F), VmValue::Float(0.0F)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToIntBits", "(F)I", {VmValue::Float(std::numeric_limits<float>::quiet_NaN())}), 0x7fc00000);
    const auto raw_nan = std::bit_cast<float>(0x7fc01234U);
    ExpectInt(vm.CallStatic("Ljava/lang/Float;", "floatToRawIntBits", "(F)I", {VmValue::Float(raw_nan)}), 0x7fc01234);
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(-0.0F)})) == "-0.0");
    for (const auto& [value, text] : std::vector<std::pair<float, std::string>>{
             {0.0F,"0.0"},{1.0F,"1.0"},{-1.0F,"-1.0"},{0.5F,"0.5"},
             {1.5F,"1.5"},{1.0e8F,"1.0E8"},{1.0e-4F,"1.0E-4"},
             {std::numeric_limits<float>::denorm_min(),"1.4E-45"},
             {std::bit_cast<float>(0x00000003U),"4.2E-45"}}) {
        CAPTURE(text);
        CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString",
            "(F)Ljava/lang/String;", {VmValue::Float(value)})) == text);
    }
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(std::numeric_limits<float>::quiet_NaN())})) == "NaN");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toString", "(F)Ljava/lang/String;", {VmValue::Float(-std::numeric_limits<float>::infinity())})) == "-Infinity");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Float;", "toHexString",
                                  "(F)Ljava/lang/String;",
                                  {VmValue::Float(std::numeric_limits<float>::denorm_min())})) ==
          "0x0.000002p-126");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Float;", "toHexString", "(F)Ljava/lang/String;",
              {VmValue::Float(std::bit_cast<float>(0x007fffffU))})) ==
          "0x0.fffffep-126");
    CHECK(as_string(vm.CallStatic("Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;", {VmValue::Double(std::numeric_limits<double>::infinity())})) == "Infinity");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toString", "(D)Ljava/lang/String;",
              {VmValue::Double(std::numeric_limits<double>::denorm_min())})) ==
          "4.9E-324");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
              {VmValue::Double(std::numeric_limits<double>::denorm_min())})) ==
          "0x0.0000000000001p-1022");
    CHECK(as_string(vm.CallStatic(
              "Ljava/lang/Double;", "toHexString", "(D)Ljava/lang/String;",
              {VmValue::Double(
                  std::bit_cast<double>(0x000fffffffffffffULL))})) ==
          "0x0.fffffffffffffp-1022");
    const auto parsed_half = vm.CallStatic("Ljava/lang/Double;", "parseDouble", "(Ljava/lang/String;)D", {string("  +0.5  ")});
    REQUIRE_FALSE(parsed_half.exception.IsValid());
    CHECK(parsed_half.value.AsDouble() == 0.5);
    ExpectInt(vm.CallStatic("Ljava/lang/Double;", "compare", "(DD)I", {VmValue::Double(std::numeric_limits<double>::quiet_NaN()), VmValue::Double(std::numeric_limits<double>::infinity())}), 1);
    const auto raw_double_nan = std::bit_cast<double>(0x7ff8000000001234ULL);
    expect_long(vm.CallStatic("Ljava/lang/Double;", "doubleToRawLongBits", "(D)J", {VmValue::Double(raw_double_nan)}), static_cast<std::int64_t>(0x7ff8000000001234ULL));
    const auto infinity = vm.CallStatic("Ljava/lang/Float;", "valueOf", "(F)Ljava/lang/Float;", {VmValue::Float(std::numeric_limits<float>::infinity())});
    REQUIRE_FALSE(infinity.exception.IsValid());
    ExpectInt(call_on("Ljava/lang/Float;", infinity.value.ref, "intValue", "()I"), std::numeric_limits<std::int32_t>::max());
    const auto true_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(1)});
    const auto true_two = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Ljava/lang/String;)Ljava/lang/Boolean;", {string("TRUE")});
    REQUIRE_FALSE(true_one.exception.IsValid());
    REQUIRE_FALSE(true_two.exception.IsValid());
    CHECK(true_one.value.ref == true_two.value.ref);
    const auto false_one = vm.CallStatic("Ljava/lang/Boolean;", "valueOf", "(Z)Ljava/lang/Boolean;", {VmValue::Int(0)});
    REQUIRE_FALSE(false_one.exception.IsValid());
    CHECK(false_one.value.ref != true_one.value.ref);
    ExpectInt(call_on("Ljava/lang/Boolean;", true_one.value.ref, "hashCode", "()I"), 1231);
    ExpectInt(call_on("Ljava/lang/Boolean;", false_one.value.ref, "compareTo", "(Ljava/lang/Boolean;)I", {VmValue::Ref(true_one.value.ref)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xd800), VmValue::Int(0xdc00)}), 0x10000);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isValidCodePoint", "(I)Z", {VmValue::Int(0x110000)}), 0);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isSupplementaryCodePoint", "(I)Z", {VmValue::Int(0x10ffff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('Z'), VmValue::Int(36)}), 35);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('1'), VmValue::Int(2)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "digit", "(II)I", {VmValue::Int('a'), VmValue::Int(10)}), -1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "forDigit", "(II)C", {VmValue::Int(15), VmValue::Int(16)}), 'f');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLetterOrDigit", "(I)Z", {VmValue::Int('Q')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toLowerCase", "(I)I", {VmValue::Int('Q')}), 'q');
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isWhitespace", "(I)Z", {VmValue::Int('\n')}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isHighSurrogate", "(C)Z", {VmValue::Int(0xdbff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "isLowSurrogate", "(C)Z", {VmValue::Int(0xdfff)}), 1);
    ExpectInt(vm.CallStatic("Ljava/lang/Character;", "toCodePoint", "(CC)I", {VmValue::Int(0xdbff), VmValue::Int(0xdfff)}), 0x10ffff);
    static_cast<void>(
        vm.interpreter.SetSystemProperty("feature.enabled", "TrUe"));
    ExpectInt(vm.CallStatic("Ljava/lang/Boolean;", "getBoolean", "(Ljava/lang/String;)Z", {string("feature.enabled")}), 1);
    CHECK(static_bits("Ljava/lang/Byte;", "MAX_VALUE", "B") == 127U);
    CHECK(static_bits("Ljava/lang/Short;", "MIN_VALUE", "S") == 0xffff8000U);
    CHECK(static_bits("Ljava/lang/Integer;", "MIN_VALUE", "I") == 0x80000000U);
    CHECK(static_bits("Ljava/lang/Long;", "MAX_VALUE", "J") == 0x7fffffffffffffffULL);
    CHECK(static_bits("Ljava/lang/Float;", "NaN", "F") == 0x7fc00000U);
    CHECK(static_bits("Ljava/lang/Float;", "MIN_NORMAL", "F") == 0x00800000U);
    CHECK(static_bits("Ljava/lang/Double;", "MIN_NORMAL", "D") == 0x0010000000000000ULL);
    CHECK(static_bits("Ljava/lang/Character;", "MAX_CODE_POINT", "I") == 0x10ffffU);
    CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits(
        "Ljava/lang/Boolean;", "TRUE", "Ljava/lang/Boolean;"))} ==
        true_one.value.ref);
    for (const auto& [owner, primitive] :
         std::vector<std::pair<std::string, std::string>>{
             {"Ljava/lang/Byte;","B"},{"Ljava/lang/Short;","S"},
             {"Ljava/lang/Integer;","I"},{"Ljava/lang/Long;","J"},
             {"Ljava/lang/Float;","F"},{"Ljava/lang/Double;","D"},
             {"Ljava/lang/Boolean;","Z"},{"Ljava/lang/Character;","C"}}) {
        CAPTURE(owner);
        CHECK(VmObjectRef{static_cast<std::uint32_t>(static_bits(
                  owner.c_str(), "TYPE", "Ljava/lang/Class;"))} ==
              vm.model.ClassObject(vm.linker.ResolveDescriptor(primitive)));
    }
}

TEST_CASE("dexvm API 19 java.lang throwable inventory is complete") {
    const std::vector<std::pair<std::string, std::string>> inventory = {
        {"AbstractMethodError", "IncompatibleClassChangeError"},
        {"ArithmeticException", "RuntimeException"},
        {"ArrayIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"ArrayStoreException", "RuntimeException"},
        {"AssertionError", "Error"},
        {"ClassCastException", "RuntimeException"},
        {"ClassCircularityError", "LinkageError"},
        {"ClassFormatError", "LinkageError"},
        {"ClassNotFoundException", "ReflectiveOperationException"},
        {"CloneNotSupportedException", "Exception"},
        {"EnumConstantNotPresentException", "RuntimeException"},
        {"Error", "Throwable"},
        {"Exception", "Throwable"},
        {"ExceptionInInitializerError", "LinkageError"},
        {"IllegalAccessError", "IncompatibleClassChangeError"},
        {"IllegalAccessException", "ReflectiveOperationException"},
        {"IllegalArgumentException", "RuntimeException"},
        {"IllegalMonitorStateException", "RuntimeException"},
        {"IllegalStateException", "RuntimeException"},
        {"IllegalThreadStateException", "IllegalArgumentException"},
        {"IncompatibleClassChangeError", "LinkageError"},
        {"IndexOutOfBoundsException", "RuntimeException"},
        {"InstantiationError", "IncompatibleClassChangeError"},
        {"InstantiationException", "ReflectiveOperationException"},
        {"InternalError", "VirtualMachineError"},
        {"InterruptedException", "Exception"},
        {"LinkageError", "Error"},
        {"NegativeArraySizeException", "RuntimeException"},
        {"NoClassDefFoundError", "LinkageError"},
        {"NoSuchFieldError", "IncompatibleClassChangeError"},
        {"NoSuchFieldException", "ReflectiveOperationException"},
        {"NoSuchMethodError", "IncompatibleClassChangeError"},
        {"NoSuchMethodException", "ReflectiveOperationException"},
        {"NullPointerException", "RuntimeException"},
        {"NumberFormatException", "IllegalArgumentException"},
        {"OutOfMemoryError", "VirtualMachineError"},
        {"ReflectiveOperationException", "Exception"},
        {"RuntimeException", "Exception"},
        {"SecurityException", "RuntimeException"},
        {"StackOverflowError", "VirtualMachineError"},
        {"StringIndexOutOfBoundsException", "IndexOutOfBoundsException"},
        {"ThreadDeath", "Error"},
        {"Throwable", "Object"},
        {"TypeNotPresentException", "RuntimeException"},
        {"UnknownError", "VirtualMachineError"},
        {"UnsatisfiedLinkError", "LinkageError"},
        {"UnsupportedClassVersionError", "ClassFormatError"},
        {"UnsupportedOperationException", "RuntimeException"},
        {"VerifyError", "LinkageError"},
        {"VirtualMachineError", "Error"},
    };
    REQUIRE(inventory.size() == 50U);

    const auto catalog = CoreIntrinsicCatalog();
    std::map<std::string, std::size_t> descriptor_counts;
    for (const auto& declaration : catalog) {
        ++descriptor_counts[declaration.descriptor];
    }
    for (const auto& [name, superclass] : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        CHECK(descriptor_counts[descriptor] == 1U);
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        REQUIRE(declaration->superclass.has_value());
        CHECK(*declaration->superclass ==
              "Ljava/lang/" + superclass + ";");
    }
}

TEST_CASE("dexvm API 19 throwable representative shapes are source-faithful") {
    const auto catalog = CoreIntrinsicCatalog();
    const auto declaration = [&catalog](const std::string& descriptor)
        -> const IntrinsicClassDecl& {
        const auto found = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(found != catalog.end());
        return *found;
    };
    const auto methods = [](const IntrinsicClassDecl& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto& method : java_class.methods) {
            result.emplace(method.name, method.descriptor);
        }
        return result;
    };
    const auto fields = [](const IntrinsicClassDecl& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto& field : java_class.fields) {
            if (!field.is_static) {
                result.emplace(field.name, field.descriptor);
            }
        }
        return result;
    };

    CHECK(methods(declaration("Ljava/lang/AssertionError;")) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"<init>", "(Ljava/lang/Object;)V"},
              {"<init>", "(Z)V"},
              {"<init>", "(C)V"},
              {"<init>", "(I)V"},
              {"<init>", "(J)V"},
              {"<init>", "(F)V"},
              {"<init>", "(D)V"},
          });

    const auto& class_not_found =
        declaration("Ljava/lang/ClassNotFoundException;");
    CHECK(fields(class_not_found) ==
          std::set<std::pair<std::string, std::string>>{
              {"ex", "Ljava/lang/Throwable;"}});
    CHECK(methods(class_not_found) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "()V"},
              {"<init>", "(Ljava/lang/String;)V"},
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"getCause", "()Ljava/lang/Throwable;"},
              {"getException", "()Ljava/lang/Throwable;"},
          });

    const auto& enum_missing =
        declaration("Ljava/lang/EnumConstantNotPresentException;");
    CHECK(fields(enum_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"constantName", "Ljava/lang/String;"},
              {"enumType", "Ljava/lang/Class;"},
          });
    CHECK(methods(enum_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>", "(Ljava/lang/Class;Ljava/lang/String;)V"},
              {"constantName", "()Ljava/lang/String;"},
              {"enumType", "()Ljava/lang/Class;"},
          });

    const auto& initializer =
        declaration("Ljava/lang/ExceptionInInitializerError;");
    CHECK(fields(initializer) ==
          std::set<std::pair<std::string, std::string>>{
              {"exception", "Ljava/lang/Throwable;"}});
    CHECK(methods(initializer).contains(
        {"<init>", "(Ljava/lang/Throwable;)V"}));
    CHECK(methods(initializer).contains(
        {"getException", "()Ljava/lang/Throwable;"}));
    CHECK(methods(initializer).contains(
        {"getCause", "()Ljava/lang/Throwable;"}));

    const auto& type_missing =
        declaration("Ljava/lang/TypeNotPresentException;");
    CHECK(fields(type_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"typeName", "Ljava/lang/String;"}});
    CHECK(methods(type_missing) ==
          std::set<std::pair<std::string, std::string>>{
              {"<init>",
               "(Ljava/lang/String;Ljava/lang/Throwable;)V"},
              {"typeName", "()Ljava/lang/String;"},
          });
}

TEST_CASE("dexvm java.lang throwable implementation is one family TU") {
    const std::vector<std::string> classes = {
        "AbstractMethodError", "ArithmeticException",
        "ArrayIndexOutOfBoundsException", "ArrayStoreException",
        "AssertionError", "ClassCastException", "ClassCircularityError",
        "ClassFormatError", "ClassNotFoundException",
        "CloneNotSupportedException", "EnumConstantNotPresentException",
        "Error", "Exception", "ExceptionInInitializerError",
        "IllegalAccessError", "IllegalAccessException",
        "IllegalArgumentException", "IllegalMonitorStateException",
        "IllegalStateException", "IllegalThreadStateException",
        "IncompatibleClassChangeError", "IndexOutOfBoundsException",
        "InstantiationError", "InstantiationException", "InternalError",
        "InterruptedException", "LinkageError", "NegativeArraySizeException",
        "NoClassDefFoundError", "NoSuchFieldError", "NoSuchFieldException",
        "NoSuchMethodError", "NoSuchMethodException", "NullPointerException",
        "NumberFormatException", "OutOfMemoryError",
        "ReflectiveOperationException", "RuntimeException",
        "SecurityException", "StackOverflowError",
        "StringIndexOutOfBoundsException", "ThreadDeath", "Throwable",
        "TypeNotPresentException", "UnknownError", "UnsatisfiedLinkError",
        "UnsupportedClassVersionError", "UnsupportedOperationException",
        "VerifyError", "VirtualMachineError",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_throwables.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
}

TEST_CASE("dexvm java.lang primitive wrappers are one family TU") {
    const std::vector<std::string> classes = {
        "Number", "Byte", "Short", "Integer", "Long", "Float", "Double",
        "Boolean", "Character",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_primitive_wrappers.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangPrimitiveWrappers") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm API 19 java.lang interface inventory is complete") {
    const std::vector<std::string> inventory = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable",
        "Comparable", "Iterable", "Readable", "Runnable",
    };
    REQUIRE(inventory.size() == 8U);

    const auto catalog = CoreIntrinsicCatalog();
    std::map<std::string, std::size_t> descriptor_counts;
    for (const auto& declaration : catalog) {
        ++descriptor_counts[declaration.descriptor];
    }
    for (const auto& name : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        CHECK(descriptor_counts[descriptor] == 1U);
        const auto declaration = std::find_if(
            catalog.begin(), catalog.end(), [&](const auto& candidate) {
                return candidate.descriptor == descriptor;
            });
        REQUIRE(declaration != catalog.end());
        CHECK(declaration->is_interface);
        CHECK_FALSE(declaration->superclass.has_value());
    }
}

TEST_CASE("dexvm java.lang interfaces are one family TU") {
    const std::vector<std::string> classes = {
        "Appendable", "AutoCloseable", "CharSequence", "Cloneable",
        "Comparable", "Iterable", "Readable", "Runnable",
    };
    const auto directory = std::filesystem::path(OGPLAY_SOURCE_DIR) / "src" /
                           "runtime" / "dexvm" / "intrinsics";
    CHECK(std::filesystem::is_regular_file(
        directory / "java_lang_interfaces.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
    std::ifstream header(directory / "catalog.h", std::ios::binary);
    REQUIRE(header.good());
    const std::string source(std::istreambuf_iterator<char>(header), {});
    CHECK(source.find("AppendJavaLangInterfaces") != std::string::npos);
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK(source.find("Declare_java_lang_" + name) == std::string::npos);
    }
}

TEST_CASE("dexvm core handlers call directly") {
    Vm hit;
    const auto hit_method =
        hit.Static("Ljava/lang/Math;", "abs", "(I)I");
    REQUIRE(hit.linker.Method(hit_method).implementation);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-9)}),
              9);
    ExpectInt(hit.interpreter.Call(
                  hit_method, std::vector<VmValue>{VmValue::Int(-11)}),
              11);
}

TEST_CASE("dexvm intrinsic builder binds implementations without a registry") {
    std::uint32_t clinit_calls = 0;
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Direct;", "Ljava/lang/Object;");
    builder.StaticMethod("answer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(42);
        })
        .FinalMethod("virtualAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(43);
        })
        .VirtualMethod("overridableAnswer", "()I", [](IntrinsicContext&) {
            return VmValue::Int(44);
        })
        .ConstantInt("COUNT", "I", 41)
        .ConstantString("NAME", "direct")
        .ClassInitializer([&clinit_calls](IntrinsicContext&) {
            ++clinit_calls;
            return VmValue::Void();
        });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));

    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);
    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Direct;", "answer",
                                            "()I"),
                                  {}),
              42);
    CHECK(clinit_calls == 1U);

    const auto java_class = vm.linker.FindClass("Lbuilder/Direct;");
    REQUIRE(java_class.has_value());
    const auto instance =
        vm.interpreter.NewIntrinsicInstance("Lbuilder/Direct;");
    for (const auto& [name, expected] :
         std::vector<std::pair<std::string, std::int32_t>>{
             {"virtualAnswer", 43}, {"overridableAnswer", 44}}) {
        const auto index =
            vm.linker.FindVtableIndex(*java_class, name, "()I");
        REQUIRE(index.has_value());
        ExpectInt(vm.interpreter.Call(
                      vm.linker.Class(*java_class).vtable[*index],
                      std::vector<VmValue>{VmValue::Ref(instance)}),
                  expected);
    }

    const auto count = vm.linker.FindFieldRecursive(*java_class, "COUNT", "I");
    REQUIRE(count.has_value());
    CHECK(vm.linker.Class(*java_class).static_storage[
              vm.linker.Field(*count).slot] == 41U);
    const auto name = vm.linker.FindFieldRecursive(
        *java_class, "NAME", "Ljava/lang/String;");
    REQUIRE(name.has_value());
    const auto name_ref = VmObjectRef(vm.linker.Class(*java_class)
                                           .static_storage[
                                               vm.linker.Field(*name).slot]);
    CHECK(vm.interpreter.StringUtf8(name_ref) == "direct");
}

TEST_CASE("dexvm intrinsic call provides typed arguments and prebound fields") {
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Typed;");
    const auto count = builder.BoundInstanceField("count", "I");
    const auto total = builder.BoundInstanceField("total", "J");
    const auto ratio = builder.BoundInstanceField("ratio", "F");
    const auto score = builder.BoundInstanceField("score", "D");
    const auto name = builder.BoundInstanceField(
        "name", "Ljava/lang/String;");
    const auto calls = builder.BoundStaticField("calls", "I");

    builder.Constructor("(IJFDLjava/lang/String;)V",
        [=](IntrinsicContext& context) {
            const IntrinsicCall call(context);
            call.SetInt(count, call.Int(0));
            call.SetLong(total, call.Long(1));
            call.SetFloat(ratio, call.Float(2));
            call.SetDouble(score, call.Double(3));
            call.SetRef(name, call.NonNullRef(4, "name"));
            return VmValue::Void();
        })
        .VirtualMethod("value", "()D", [=](IntrinsicContext& context) {
            const IntrinsicCall call(context);
            return VmValue::Double(
                static_cast<double>(call.GetInt(count)) +
                static_cast<double>(call.GetLong(total)) +
                static_cast<double>(call.GetFloat(ratio)) +
                call.GetDouble(score));
        })
        .VirtualMethod("name", "()Ljava/lang/String;",
            [=](IntrinsicContext& context) {
                const IntrinsicCall call(context);
                return VmValue::Ref(call.GetRef(name));
            })
        .StaticMethod("countOf", "(Lbuilder/Typed;)I",
            [=](IntrinsicContext& context) {
                const IntrinsicCall call(context);
                return VmValue::Int(call.GetInt(count, call.NonNullRef(0, "value")));
            })
        .StaticMethod("bump", "()I", [=](IntrinsicContext& context) {
            const IntrinsicCall call(context);
            const auto next = call.GetInt(calls) + 1;
            call.SetInt(calls, next);
            return VmValue::Int(next);
        });

    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));
    const auto object = vm.interpreter.NewIntrinsicInstance("Lbuilder/Typed;");
    const auto string = vm.interpreter.NewStringUtf8("typed");
    const auto constructed = vm.interpreter.Call(
        vm.Static("Lbuilder/Typed;", "<init>",
                  "(IJFDLjava/lang/String;)V"),
        std::vector<VmValue>{
            VmValue::Ref(object), VmValue::Int(3), VmValue::Long(5),
            VmValue::Float(1.25F), VmValue::Double(2.5), VmValue::Ref(string)});
    CHECK_FALSE(constructed.exception.IsValid());

    const auto java_class = vm.linker.FindClass("Lbuilder/Typed;");
    REQUIRE(java_class.has_value());
    const auto value_index = vm.linker.FindVtableIndex(*java_class, "value", "()D");
    REQUIRE(value_index.has_value());
    const auto value = vm.interpreter.Call(
        vm.linker.Class(*java_class).vtable[*value_index],
        std::vector<VmValue>{VmValue::Ref(object)});
    REQUIRE_FALSE(value.exception.IsValid());
    CHECK(value.value.AsDouble() == doctest::Approx(11.75));

    const auto name_index = vm.linker.FindVtableIndex(
        *java_class, "name", "()Ljava/lang/String;");
    REQUIRE(name_index.has_value());
    const auto read_name = vm.interpreter.Call(
        vm.linker.Class(*java_class).vtable[*name_index],
        std::vector<VmValue>{VmValue::Ref(object)});
    REQUIRE_FALSE(read_name.exception.IsValid());
    CHECK(vm.interpreter.StringUtf8(read_name.value.ref) == "typed");

    ExpectInt(vm.interpreter.Call(
                  vm.Static("Lbuilder/Typed;", "countOf", "(Lbuilder/Typed;)I"),
                  std::vector<VmValue>{VmValue::Ref(object)}),
              3);
    const auto null_value = vm.interpreter.Call(
        vm.Static("Lbuilder/Typed;", "countOf", "(Lbuilder/Typed;)I"),
        std::vector<VmValue>{VmValue::Ref(VmObjectRef{})});
    ExpectException(vm, null_value, "Ljava/lang/NullPointerException;");
    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Typed;", "bump", "()I"), {}), 1);
    ExpectInt(vm.interpreter.Call(vm.Static("Lbuilder/Typed;", "bump", "()I"), {}), 2);
}

TEST_CASE("dexvm class initialization waits across execution contexts") {
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool initializer_entered{};
    bool release_initializer{};
    std::atomic<std::uint32_t> initializer_calls{};
    std::atomic<std::uint32_t> method_calls{};
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Concurrent;");
    builder.StaticMethod("answer", "()I", [&](IntrinsicContext&) {
               ++method_calls;
               return VmValue::Int(7);
           })
        .ClassInitializer([&](IntrinsicContext& context) {
            ++initializer_calls;
            auto& execution_lock = context.vm.ExecutionLock();
            const auto depth = execution_lock.ReleaseForBlocking();
            {
                std::unique_lock lock(gate_mutex);
                initializer_entered = true;
                gate_changed.notify_all();
                gate_changed.wait(lock, [&] { return release_initializer; });
            }
            execution_lock.ReacquireAfterBlocking(depth);
            return VmValue::Void();
        });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));
    const auto method = vm.Static("Lbuilder/Concurrent;", "answer", "()I");
    const auto first_context = vm.interpreter.CreateExecutionContext();
    const auto second_context = vm.interpreter.CreateExecutionContext();
    VmCallOutcome first;
    VmCallOutcome second;
    std::thread first_thread([&] {
        first = vm.interpreter.Call(first_context, method, {});
    });
    {
        std::unique_lock lock(gate_mutex);
        gate_changed.wait(lock, [&] { return initializer_entered; });
    }
    std::thread second_thread([&] {
        second = vm.interpreter.Call(second_context, method, {});
    });
    {
        const std::scoped_lock lock(gate_mutex);
        CHECK(method_calls.load() == 0U);
        release_initializer = true;
    }
    gate_changed.notify_all();
    first_thread.join();
    second_thread.join();

    ExpectInt(first, 7);
    ExpectInt(second, 7);
    CHECK(initializer_calls.load() == 1U);
    CHECK(method_calls.load() == 2U);
    vm.interpreter.DiscardExecutionContext(first_context);
    vm.interpreter.DiscardExecutionContext(second_context);
}

TEST_CASE("dexvm intrinsic builder rejects invalid declarations at build") {
    auto expect_invalid = [](IntrinsicClassBuilder builder) {
        bool rejected = false;
        try {
            static_cast<void>(std::move(builder).Build());
        } catch (const DexVmError& error) {
            rejected = true;
            CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        }
        CHECK(rejected);
    };

    auto duplicate = IntrinsicClassBuilder::Class("Lbuilder/Duplicate;");
    duplicate.UnimplementedStatic("answer", "()I")
        .UnimplementedStatic("answer", "()I");
    expect_invalid(std::move(duplicate));

    auto invalid_descriptor = IntrinsicClassBuilder::Class("Lbuilder/Invalid;");
    invalid_descriptor.UnimplementedStatic("answer", "(I");
    expect_invalid(std::move(invalid_descriptor));

    auto invalid_interface = IntrinsicClassBuilder::Interface("Lbuilder/Interface;");
    invalid_interface.InstanceField("state", "I");
    expect_invalid(std::move(invalid_interface));

    auto constant_range = IntrinsicClassBuilder::Class("Lbuilder/Range;");
    constant_range.ConstantInt("MAX", "B", 128);
    expect_invalid(std::move(constant_range));

    // Declaration-time validation rejects before Build() is reached.
    const auto reject_direct = [](auto declare) {
        bool rejected = false;
        try {
            declare();
        } catch (const DexVmError& error) {
            rejected = true;
            CHECK(error.Reason() == DexVmErrorReason::internal_invariant);
        }
        CHECK(rejected);
    };
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/BadCtor;");
        builder.Constructor("()I",
                            [](IntrinsicContext&) { return VmValue::Int(1); });
    });
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/Empty;");
        builder.StaticMethod("answer", "()I", {});
    });
    reject_direct([] {
        auto builder = IntrinsicClassBuilder::Class("Lbuilder/Reserved;");
        builder.FinalMethod("<init>", "()V",
                            [](IntrinsicContext&) { return VmValue::Void(); });
    });
}

TEST_CASE("dexvm intrinsic builder maps headers, members, and constants") {
    const auto root = IntrinsicClassBuilder::RootClass("Ljava/lang/Object;")
                          .Build();
    CHECK(root.descriptor == "Ljava/lang/Object;");
    CHECK_FALSE(root.superclass.has_value());
    CHECK_FALSE(root.is_interface);

    const auto callback = IntrinsicClassBuilder::Interface(
                              "Lbuilder/Callback;", {"Ljava/lang/Runnable;"})
                              .Build();
    CHECK(callback.is_interface);
    REQUIRE(callback.interfaces.size() == 1U);
    CHECK(callback.interfaces[0] == "Ljava/lang/Runnable;");

    auto builder = IntrinsicClassBuilder::Class(
        "Lbuilder/Header;", "Ljava/lang/Object;", {"Ljava/lang/Runnable;"});
    builder.InstanceField("id", "I");
    builder.StaticField("counter", "I");
    builder.ConstantInt("MAX", "B", 127);
    builder.Constructor("()V",
                        [](IntrinsicContext&) { return VmValue::Void(); });
    builder.StaticMethod("create", "()Lbuilder/Header;",
        [](IntrinsicContext& context) { return VmValue::Ref(context.receiver); });
    builder.VirtualMethod("describe", "()Ljava/lang/String;",
        [](IntrinsicContext& context) { return VmValue::Ref(context.receiver); });
    builder.FinalMethod("id", "()I", [](IntrinsicContext&) { return VmValue::Int(7); });
    builder.UnimplementedConstructor("(I)V");
    builder.UnimplementedStatic("parse", "(Ljava/lang/String;)I");
    builder.UnimplementedVirtual("run", "()V");
    builder.UnimplementedFinal("close", "()V");
    const auto decl = std::move(builder).Build();
    CHECK_FALSE(decl.is_interface);
    REQUIRE(decl.superclass.has_value());
    CHECK(*decl.superclass == "Ljava/lang/Object;");
    REQUIRE(decl.interfaces.size() == 1U);
    CHECK(decl.interfaces[0] == "Ljava/lang/Runnable;");

    const auto find_method = [&decl](const std::string& name,
                                     const std::string& descriptor) {
        const auto it = std::find_if(
            decl.methods.begin(), decl.methods.end(),
            [&](const IntrinsicMethodDecl& method) {
                return method.name == name && method.descriptor == descriptor;
            });
        REQUIRE(it != decl.methods.end());
        return *it;
    };

    const auto init = find_method("<init>", "()V");
    CHECK_FALSE(init.is_static);
    CHECK_FALSE(init.overridable);
    CHECK(init.implementation);

    const auto create = find_method("create", "()Lbuilder/Header;");
    CHECK(create.is_static);
    CHECK_FALSE(create.overridable);
    CHECK(create.implementation);

    const auto describe = find_method("describe", "()Ljava/lang/String;");
    CHECK_FALSE(describe.is_static);
    CHECK(describe.overridable);
    CHECK(describe.implementation);

    const auto id = find_method("id", "()I");
    CHECK_FALSE(id.is_static);
    CHECK_FALSE(id.overridable);

    find_method("<init>", "(I)V");
    CHECK_FALSE(find_method("parse", "(Ljava/lang/String;)I").implementation);
    CHECK(find_method("run", "()V").overridable);
    CHECK_FALSE(find_method("close", "()V").overridable);

    const auto find_field = [&decl](const std::string& name) {
        const auto it = std::find_if(
            decl.fields.begin(), decl.fields.end(),
            [&](const IntrinsicFieldDecl& field) { return field.name == name; });
        REQUIRE(it != decl.fields.end());
        return *it;
    };
    CHECK_FALSE(find_field("id").is_static);
    CHECK(find_field("counter").is_static);
    const auto max = find_field("MAX");
    CHECK(max.has_constant);
    CHECK(max.integral == 127);
}

TEST_CASE("dexvm declaration miss uses the owner signature and repeats") {
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/Missing;", "Ljava/lang/Object;");
    builder.UnimplementedStatic("answer", "()I");
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));
    const auto method = vm.Static("Lbuilder/Missing;", "answer", "()I");

    for (std::uint32_t call = 0; call < 2; ++call) {
        ExpectException(vm, vm.interpreter.Call(method, {}),
                        "Ljava/lang/UnsatisfiedLinkError;");
    }
    const auto hits = vm.ledger.Unimplemented();
    REQUIRE(hits.size() == 1U);
    CHECK(hits[0].id ==
          "dexvm.intrinsic.Lbuilder/Missing;.answer()I");
    CHECK(hits[0].count == 2U);
}

TEST_CASE("dexvm System properties are deterministic and mutable") {
    Vm vm;
    const auto get = vm.Static(
        "Ljava/lang/System;", "getProperty",
        "(Ljava/lang/String;)Ljava/lang/String;");
    const auto set = vm.Static(
        "Ljava/lang/System;", "setProperty",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    const auto string = [&vm](const std::string& value) {
        return VmValue::Ref(vm.interpreter.NewStringUtf8(value));
    };
    const auto get_property = [&vm, get, &string](const std::string& key) {
        return vm.interpreter.Call(
            get, std::vector<VmValue>{string(key)});
    };

    for (const auto& [key, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"file.separator", "/"},
             {"line.separator", "\n"},
             {"path.separator", ":"}}) {
        const auto outcome = get_property(key);
        REQUIRE_FALSE(outcome.exception.IsValid());
        REQUIRE(outcome.value.ref.IsValid());
        CHECK(vm.interpreter.StringUtf8(outcome.value.ref) == expected);
    }

    const auto missing = get_property("ogplay.missing");
    REQUIRE_FALSE(missing.exception.IsValid());
    CHECK_FALSE(missing.value.ref.IsValid());

    const auto first = vm.interpreter.Call(
        set, std::vector<VmValue>{string("ogplay.test"), string("first")});
    REQUIRE_FALSE(first.exception.IsValid());
    CHECK_FALSE(first.value.ref.IsValid());
    const auto first_read = get_property("ogplay.test");
    REQUIRE_FALSE(first_read.exception.IsValid());
    REQUIRE(first_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(first_read.value.ref) == "first");

    const auto second = vm.interpreter.Call(
        set, std::vector<VmValue>{string("ogplay.test"), string("second")});
    REQUIRE_FALSE(second.exception.IsValid());
    REQUIRE(second.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second.value.ref) == "first");
    const auto second_read = get_property("ogplay.test");
    REQUIRE_FALSE(second_read.exception.IsValid());
    REQUIRE(second_read.value.ref.IsValid());
    CHECK(vm.interpreter.StringUtf8(second_read.value.ref) == "second");

    ExpectException(
        vm,
        vm.interpreter.Call(
            get, std::vector<VmValue>{VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
    ExpectException(vm,
                    vm.interpreter.Call(
                        get, std::vector<VmValue>{string("")}),
                    "Ljava/lang/IllegalArgumentException;");
    ExpectException(
        vm,
        vm.interpreter.Call(
            set, std::vector<VmValue>{string("ogplay.null"),
                                      VmValue::Ref(VmObjectRef{})}),
        "Ljava/lang/NullPointerException;");
}

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
    model_config.heap_budget_bytes = 40;
    Vm vm(config, model_config);
    const auto outcome = vm.CallStatic("LFlow;", "sumArray", "()I");
    ExpectException(vm, outcome, "Ljava/lang/OutOfMemoryError;");
    });
}

TEST_CASE("dexvm GC watermark bounds allocation and zero disables collection") {
    WithEachBackend([](InterpreterConfig config) {
    JavaObjectModelConfig enabled;
    enabled.heap_budget_bytes = 128;
    enabled.gc_watermark_percent = 75;
    Vm collecting(config, enabled);
    ExpectInt(collecting.CallStatic("LFlow;", "gcChurn", "()I"), 10);
    CHECK(collecting.interpreter.Stats().gc_collections > 0);
    CHECK(collecting.interpreter.Stats().gc_freed_bytes > 0);
    CHECK(collecting.model.AllocatedBytes() <= enabled.heap_budget_bytes);

    JavaObjectModelConfig disabled = enabled;
    disabled.gc_watermark_percent = 0;
    Vm gc_a(config, disabled);
    const auto outcome = gc_a.CallStatic("LFlow;", "gcChurn", "()I");
    ExpectException(gc_a, outcome, "Ljava/lang/OutOfMemoryError;");
    CHECK(gc_a.interpreter.Stats().gc_collections == 0);
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
                      "DexVM fault instruction: invoke-static opcode=0x71 "
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
    heap.heap_budget_bytes = 128;
    heap.gc_watermark_percent = 75;
    Vm vm(config, heap);

    ExpectInt(vm.CallStatic("LClinitUser;", "read", "()I"), 55);
    ExpectInt(vm.CallStatic("LArith;", "divideCaught", "(II)I",
                            {VmValue::Int(1), VmValue::Int(0)}),
              -99);
    ExpectInt(vm.CallStatic("LFlow;", "gcChurn", "()I"), 10);
    const auto monitor = vm.CallStatic("LWaitProbe;", "signal", "()V");
    REQUIRE_FALSE(monitor.exception.IsValid());

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
