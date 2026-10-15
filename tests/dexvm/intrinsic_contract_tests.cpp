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
    override.OverrideMethod("hashCode", "()I", [](IntrinsicContext&) {
        return VmValue::Int(777);
    });
    const auto existing_throwable = std::make_shared<VmObjectRef>();
    auto throwing = IntrinsicClassBuilder::Class("Lidentity/Throwing;");
    throwing.OverrideMethod(
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

TEST_CASE("dexvm core handlers call directly") {
    Vm hit;
    const auto method = hit.Static("Ljava/lang/Math;", "floor", "(D)D");
    REQUIRE(hit.linker.Method(method).implementation);
    for (const auto value : {-9.2, -11.2}) {
        const auto outcome = hit.interpreter.Call(method, std::vector{VmValue::Double(value)});
        REQUIRE_FALSE(outcome.exception.IsValid());
        CHECK(outcome.value.AsDouble() == std::floor(value));
    }
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

TEST_CASE("DVM-98 intrinsic enum builder publishes generated enum behavior") {
    std::size_t initialized_constants = 0;
    std::size_t published_constants = 0;
    IntrinsicEnumBuilder builder("Lbuilder/Color;", {"RED", "GREEN"});
    const auto payload =
        builder.ClassBuilder().BoundInstanceField("payload", "I");
    builder.WithConstantInitializer(
        [payload, &initialized_constants](IntrinsicContext& call,
                                          VmObjectRef value,
                                          std::string_view,
                                          std::size_t ordinal) {
            IntrinsicCall(call).SetInt(
                payload, value, static_cast<std::int32_t>(10U + ordinal));
            ++initialized_constants;
        });
    builder.AfterConstants(
        [&published_constants](IntrinsicContext&,
                               std::span<const VmObjectRef> constants) {
            published_constants = constants.size();
        });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));

    const auto color = vm.linker.FindClass("Lbuilder/Color;");
    const auto enum_class = vm.linker.FindClass("Ljava/lang/Enum;");
    REQUIRE(color.has_value());
    REQUIRE(enum_class.has_value());
    REQUIRE(vm.linker.Class(*color).super.has_value());
    CHECK(*vm.linker.Class(*color).super == *enum_class);
    CHECK((vm.linker.Class(*color).access_flags & (kAccFinal | kAccEnum)) ==
          (kAccFinal | kAccEnum));

    const auto first_values = vm.interpreter.Call(
        vm.Static("Lbuilder/Color;", "values", "()[Lbuilder/Color;"), {});
    const auto second_values = vm.interpreter.Call(
        vm.Static("Lbuilder/Color;", "values", "()[Lbuilder/Color;"), {});
    REQUIRE_FALSE(first_values.exception.IsValid());
    REQUIRE_FALSE(second_values.exception.IsValid());
    CHECK(first_values.value.ref != second_values.value.ref);
    CHECK(vm.model.ArrayLength(first_values.value.ref) == 2);
    CHECK(initialized_constants == 2U);
    CHECK(published_constants == 2U);

    const auto red = vm.model.GetObjectElement(first_values.value.ref, 0);
    const auto green = vm.model.GetObjectElement(first_values.value.ref, 1);
    const std::array red_argument{VmValue::Ref(red)};
    const std::array green_receiver{VmValue::Ref(green)};
    CHECK(vm.interpreter.StringUtf8(
              vm.interpreter.Call(vm.Virtual("Ljava/lang/Enum;", "name",
                                             "()Ljava/lang/String;"),
                                  red_argument)
                  .value.ref) == "RED");
    CHECK(vm.interpreter.Call(vm.Virtual("Ljava/lang/Enum;", "ordinal",
                                         "()I"),
                              green_receiver)
              .value.AsInt() == 1);
    const auto payload_field = vm.linker.FindFieldRecursive(
        *color, "payload", "I");
    REQUIRE(payload_field.has_value());
    CHECK(vm.model.InstanceSlots(red)[vm.linker.Field(*payload_field).slot]
              .bits == 10U);
    CHECK(vm.model.InstanceSlots(green)[vm.linker.Field(*payload_field).slot]
              .bits == 11U);

    const std::array green_argument{
        VmValue::Ref(vm.interpreter.NewStringUtf8("GREEN"))};
    const auto by_name = vm.interpreter.Call(
        vm.Static("Lbuilder/Color;", "valueOf",
                  "(Ljava/lang/String;)Lbuilder/Color;"),
        green_argument);
    REQUIRE_FALSE(by_name.exception.IsValid());
    CHECK(by_name.value.ref == green);

    const std::array null_argument{VmValue::Ref(VmObjectRef{})};
    const auto null_name = vm.interpreter.Call(
        vm.Static("Lbuilder/Color;", "valueOf",
                  "(Ljava/lang/String;)Lbuilder/Color;"),
        null_argument);
    REQUIRE(null_name.exception.IsValid());
    CHECK(vm.linker.Class(null_name.exception_class).descriptor ==
          "Ljava/lang/NullPointerException;");
    const std::array unknown_argument{
        VmValue::Ref(vm.interpreter.NewStringUtf8("BLUE"))};
    const auto unknown_name = vm.interpreter.Call(
        vm.Static("Lbuilder/Color;", "valueOf",
                  "(Ljava/lang/String;)Lbuilder/Color;"),
        unknown_argument);
    REQUIRE(unknown_name.exception.IsValid());
    CHECK(vm.linker.Class(unknown_name.exception_class).descriptor ==
          "Ljava/lang/IllegalArgumentException;");

    const auto red_field = vm.linker.FindFieldRecursive(
        *color, "RED", "Lbuilder/Color;");
    const auto values_field = vm.linker.FindFieldRecursive(
        *color, "$VALUES", "[Lbuilder/Color;");
    REQUIRE(red_field.has_value());
    REQUIRE(values_field.has_value());
    CHECK(vm.linker.Field(*red_field).access_flags ==
          (kAccPublic | kAccStatic | kAccFinal | kAccEnum));
    CHECK(vm.linker.Field(*values_field).access_flags ==
          (kAccPrivate | kAccStatic | kAccFinal | kAccSynthetic));
}

TEST_CASE("DVM-96 intrinsic boundary validates arguments receivers and returns") {
    auto builder = IntrinsicClassBuilder::Class("Lbuilder/BadShape;");
    builder.StaticMethod("badReturn", "()I", [](IntrinsicContext&) {
        return VmValue::Ref(VmObjectRef{});
    });
    builder.StaticMethod("takesInt", "(I)V", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    builder.VirtualMethod("instance", "()V", [](IntrinsicContext&) {
        return VmValue::Void();
    });
    std::vector<IntrinsicClassDecl> catalog;
    catalog.push_back(std::move(builder).Build());
    IntrinsicVm vm(std::move(catalog));

    CHECK_THROWS_AS(
        (void)vm.interpreter.Call(vm.Static("Lbuilder/BadShape;", "badReturn",
                                            "()I"),
                                  {}),
        DexVmError);
    CHECK_THROWS_AS(
        (void)vm.interpreter.Call(vm.Static("Lbuilder/BadShape;", "takesInt",
                                            "(I)V"),
                                  std::vector<VmValue>{
                                      VmValue::Ref(VmObjectRef{})}),
        DexVmError);

    const auto object = vm.interpreter.NewIntrinsicInstance(
        "Ljava/lang/Object;");
    CHECK_THROWS_AS(
        (void)vm.interpreter.Call(vm.Virtual("Lbuilder/BadShape;", "instance",
                                             "()V"),
                                  std::vector<VmValue>{VmValue::Ref(object)}),
        DexVmError);
}

TEST_CASE("DVM-94 method resolution cache is isolated by invoke kind") {
    Vm vm;
    const auto& image = vm.linker.Image();
    const auto find_method_index = [&](const std::string_view wanted) {
        for (std::uint32_t index = 0; index < image.methods.size(); ++index) {
            const auto& text =
                image.strings[image.methods[index].name_string_index].value;
            const auto name = ogplay::core::Utf16ToUtf8(
                text, ogplay::core::InvalidUtf16Policy::reject);
            if (!name.has_value()) {
                FAIL("method name is not valid UTF-16");
                return std::uint32_t{};
            }
            if (*name == wanted) return index;
        }
        FAIL("method is missing from fixture: ", wanted);
        return std::uint32_t{};
    };

    const auto constructor = find_method_index("<init>");
    const auto direct = vm.linker.ResolveMethodIndex(
        constructor, InvokeKind::private_direct);
    CHECK(direct.kind == InvokeKind::constructor);
    CHECK(vm.linker.Method(direct.method).name == "<init>");
    CHECK_THROWS_AS((void)vm.linker.ResolveMethodIndex(
                        constructor, InvokeKind::static_call),
                    DexVmError);
    CHECK_THROWS_AS((void)vm.linker.ResolveMethodIndex(
                        constructor, InvokeKind::virtual_call),
                    DexVmError);
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

TEST_CASE("DVM-105 guest native admission is explicit and has no intrinsic handler") {
    auto b = IntrinsicClassBuilder::Class("Ltest/GuestNative;");
    b.GuestNativeStatic("call", "(J)I");
    const auto declaration = std::move(b).Build();
    REQUIRE(declaration.methods.size() == 1);
    CHECK(declaration.methods[0].guest_native);
    CHECK(declaration.methods[0].is_static);
    CHECK((declaration.methods[0].access_flags & kAccNative) != 0);
    CHECK_FALSE(static_cast<bool>(declaration.methods[0].implementation));
    DexClassLinker linker;
    linker.RegisterIntrinsics(std::array{declaration});
    const auto type = *linker.FindClass("Ltest/GuestNative;");
    const auto method = *linker.FindDirectMethod(type, "call", "(J)I");
    CHECK(linker.Method(method).kind == MethodKind::native);
    auto invalid = declaration;
    invalid.methods[0].implementation = [](IntrinsicContext&) { return VmValue::Int(0); };
    DexClassLinker rejected;
    CHECK_THROWS_AS(rejected.RegisterIntrinsics(std::array{invalid}), DexVmError);
}
