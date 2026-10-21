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

    Vm vm;
    for (const auto& [name, superclass] : inventory) {
        const auto descriptor = "Ljava/lang/" + name + ";";
        CAPTURE(descriptor);
        const auto type = vm.linker.ResolveDescriptor(descriptor);
        const auto& cls = vm.linker.Class(type);
        REQUIRE(cls.is_boot_dex);
        REQUIRE(cls.super.has_value());
        CHECK(vm.linker.Class(*cls.super).descriptor == "Ljava/lang/" + superclass + ";");
        for (const auto& group : {cls.own_direct_methods, cls.own_virtual_methods}) {
            for (const auto id : group) {
                const auto& method = vm.linker.Method(id);
                const bool stack_native = name == "Throwable" &&
                    (method.name == "nativeFillInStackTrace" || method.name == "nativeGetStackTrace");
                CHECK(method.kind == (stack_native ? MethodKind::intrinsic : MethodKind::interpreted));
            }
        }
    }
}

TEST_CASE("dexvm API 19 throwable representative shapes are source-faithful") {
    Vm vm;
    const auto declaration = [&vm](const std::string& descriptor) -> const LinkedClass& {
        const auto type = vm.linker.ResolveDescriptor(descriptor);
        vm.linker.EnsureClassLinked(type);
        return vm.linker.Class(type);
    };
    const auto methods = [&vm](const LinkedClass& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto& group : {java_class.own_direct_methods, java_class.own_virtual_methods}) {
            for (const auto id : group) {
                const auto& method = vm.linker.Method(id);
                result.emplace(method.name, method.descriptor);
            }
        }
        return result;
    };
    const auto fields = [&vm](const LinkedClass& java_class) {
        std::set<std::pair<std::string, std::string>> result;
        for (const auto id : java_class.own_instance_fields) {
            const auto& field = vm.linker.Field(id);
            result.emplace(field.name, field.descriptor);
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

TEST_CASE("dexvm Throwable native boundary stays in lang family TU") {
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
    CHECK(std::filesystem::is_regular_file(directory / "java_lang.cpp"));
    for (const auto& name : classes) {
        CAPTURE(name);
        CHECK_FALSE(std::filesystem::exists(
            directory / ("java_lang_" + name + ".cpp")));
    }
}

TEST_CASE("DVM-115 VM exception construction has bounded emergency heap and stack space") {
    WithEachBackend([](InterpreterConfig config) {
        Vm f(config, JavaObjectModelConfig{
                         .initial_heap_target_bytes = 128,
                         .heap_growth_limit_bytes = 128,
                         .maximum_heap_bytes = 128,
                         .min_free_bytes = 0,
                         .max_free_bytes = 0});
        ExpectInt(f.CallStatic("LArith;", "divideCaught", "(II)I",
                              {VmValue::Int(1), VmValue::Int(0)}), -99);
        const auto byte_array = f.linker.ResolveDescriptor("[B");
        CHECK_FALSE(f.model.SetEmergencyReserve(true)); // Construction restored the normal heap limit.
        CHECK(f.model.SetEmergencyReserve(true)); // Nesting must not replenish the reserve.
        CHECK_THROWS_AS(static_cast<void>(f.model.NewPrimitiveArray(
            byte_array, JniPrimitiveKind::byte, 70000)), DexVmError);
        f.model.SetEmergencyReserve(false);
        CHECK_THROWS_AS(static_cast<void>(f.model.NewPrimitiveArray(
            byte_array, JniPrimitiveKind::byte, 1024)), DexVmError);
    });
}

TEST_CASE("DVM-115 BootDex Throwable owns stack printing copies causes and suppression") {
    WithEachBackend([](InterpreterConfig config) {
        Vm f(config);
        auto& vm = f.interpreter;
        VmThreadRuntime threads(vm);
        std::vector<Interpreter::RootScope> roots;
        const auto keep = [&](VmObjectRef ref) {
            roots.push_back(vm.ProtectReferences(std::array{ref}));
            return ref;
        };
        const auto ok = [&](const VmCallOutcome& result) {
            REQUIRE_MESSAGE(!result.exception.IsValid(), result.exception_message);
            if (result.value.ref.IsValid()) keep(result.value.ref);
            return result.value;
        };
        const auto call = [&](VmObjectRef object, const char* name, const char* signature,
                              std::vector<VmValue> args = {}) {
            const auto type = f.model.ObjectClass(object);
            const auto slot = f.linker.FindVtableIndex(type, name, signature);
            REQUIRE(slot.has_value());
            args.insert(args.begin(), VmValue::Ref(object));
            return vm.Call(f.linker.Class(type).vtable[*slot], args);
        };
        const auto make = [&](const char* message) {
            return ok(f.CallStatic("LThrowableMessageProbe;", "createOuter",
                "(Ljava/lang/String;)Ljava/lang/Throwable;", {VmValue::Ref(vm.NewStringUtf8(message))})).ref;
        };
        const auto print = [&](VmObjectRef object) {
            return vm.StringUtf8(ok(f.CallStatic("LThrowableMessageProbe;", "print",
                "(Ljava/lang/Throwable;)Ljava/lang/String;", {VmValue::Ref(object)})).ref);
        };
        const auto top = make("top");
        const auto cause = make("cause");
        const auto suppressed = make("suppressed");
        auto trace = ok(call(top, "getStackTrace", "()[Ljava/lang/StackTraceElement;")).ref;
        REQUIRE(f.model.ArrayLength(trace) == 2);
        CHECK(vm.StringUtf8(ok(call(f.model.GetObjectElement(trace, 0), "getMethodName", "()Ljava/lang/String;")).ref) == "create");
        CHECK(vm.StringUtf8(ok(call(f.model.GetObjectElement(trace, 1), "getMethodName", "()Ljava/lang/String;")).ref) == "createOuter");
        const auto again = ok(call(top, "getStackTrace", "()[Ljava/lang/StackTraceElement;")).ref;
        CHECK(again != trace);
        f.model.SetObjectElement(trace, 0, VmObjectRef{});
        CHECK(f.model.GetObjectElement(again, 0).IsValid());
        ok(call(top, "initCause", "(Ljava/lang/Throwable;)Ljava/lang/Throwable;", {VmValue::Ref(cause)}));
        ok(call(top, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(suppressed)}));
        const auto list = ok(call(top, "getSuppressed", "()[Ljava/lang/Throwable;")).ref;
        REQUIRE(f.model.ArrayLength(list) == 1);
        CHECK(f.model.GetObjectElement(list, 0) == suppressed);
        f.model.SetObjectElement(list, 0, VmObjectRef{});
        while (roots.size() > 1) roots.pop_back(); // Only the outer exception is rooted.
        static_cast<void>(vm.CollectGarbage("throwable-java-state"));
        CHECK(ok(call(top, "getCause", "()Ljava/lang/Throwable;")).ref == cause);
        const auto expected = std::string("java.lang.Exception: top\n") +
            "\tat ThrowableMessageProbe.create(Unknown Source)\n" +
            "\tat ThrowableMessageProbe.createOuter(Unknown Source)\n" +
            "\tSuppressed: java.lang.Exception: suppressed\n\t\t... 2 more\n" +
            "Caused by: java.lang.Exception: cause\n\t... 2 more\n";
        CHECK(print(top) == expected);
        const auto rethrow = f.CallStatic("LThrowableMessageProbe;", "rethrow",
            "(Ljava/lang/Throwable;)V", {VmValue::Ref(top)});
        CHECK(rethrow.exception == top);
        CHECK(print(top) == expected);
        trace = ok(call(top, "getStackTrace", "()[Ljava/lang/StackTraceElement;")).ref;
        f.model.SetObjectElement(trace, 0, VmObjectRef{});
        const auto empty = keep(f.model.NewObjectArray(
            f.linker.ResolveDescriptor("[Ljava/lang/StackTraceElement;"),
            f.linker.ResolveDescriptor("Ljava/lang/StackTraceElement;"), 0));
        ok(call(top, "setStackTrace", "([Ljava/lang/StackTraceElement;)V", {VmValue::Ref(empty)}));
        CHECK(f.model.ArrayLength(ok(call(top, "getStackTrace", "()[Ljava/lang/StackTraceElement;")).ref) == 0);
        const auto invalid_trace = call(top, "setStackTrace", "([Ljava/lang/StackTraceElement;)V", {VmValue::Ref(trace)});
        CHECK(f.linker.Class(invalid_trace.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto null_writer = call(top, "printStackTrace", "(Ljava/io/PrintWriter;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(f.linker.Class(null_writer.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto self = call(top, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(top)});
        CHECK(f.linker.Class(self.exception_class).descriptor == "Ljava/lang/IllegalArgumentException;");
        const auto null_suppressed = call(top, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(VmObjectRef{})});
        CHECK(f.linker.Class(null_suppressed.exception_class).descriptor == "Ljava/lang/NullPointerException;");
        const auto frozen = keep(vm.NewIntrinsicInstance("Ljava/lang/Throwable;"));
        const auto ctor = f.Static("Ljava/lang/Throwable;", "<init>", "(Ljava/lang/String;Ljava/lang/Throwable;ZZ)V");
        ok(vm.Call(ctor, std::array{VmValue::Ref(frozen), VmValue::Ref(VmObjectRef{}),
            VmValue::Ref(VmObjectRef{}), VmValue::Int(0), VmValue::Int(0)}));
        ok(call(frozen, "addSuppressed", "(Ljava/lang/Throwable;)V", {VmValue::Ref(cause)}));
        CHECK(f.model.ArrayLength(ok(call(frozen, "getSuppressed", "()[Ljava/lang/Throwable;")).ref) == 0);
        ok(call(frozen, "fillInStackTrace", "()Ljava/lang/Throwable;"));
        CHECK(f.model.ArrayLength(ok(call(frozen, "getStackTrace", "()[Ljava/lang/StackTraceElement;")).ref) == 0);
        CHECK(print(frozen) == "java.lang.Throwable\n");
        const auto text = keep(vm.Model().NewString(u"A\U0001f600B"));
        CHECK(ok(call(text, "subSequence", "(II)Ljava/lang/CharSequence;", {VmValue::Int(0), VmValue::Int(4)})).ref == text);
        const auto slice = ok(call(text, "subSequence", "(II)Ljava/lang/CharSequence;", {VmValue::Int(1), VmValue::Int(3)})).ref;
        CHECK(vm.Model().StringValue(slice) == u"\U0001f600");
        const auto bad_range = call(text, "subSequence", "(II)Ljava/lang/CharSequence;", {VmValue::Int(-1), VmValue::Int(2)});
        CHECK(f.linker.Class(bad_range.exception_class).descriptor == "Ljava/lang/StringIndexOutOfBoundsException;");
    });
}

TEST_CASE("DVM-114 Throwable localized messages honor DEX overrides and preserve failures") {
    WithEachBackend([](InterpreterConfig config) {
        auto host = IntrinsicClassBuilder::Class("Lthrowable/Host;");
        host.StaticMethod("collect", "()V", [](IntrinsicContext& context) {
            static_cast<void>(context.vm.CollectGarbage("throwable-message-callback"));
            return VmValue::Void();
        });
        Vm f(config, {}, {std::move(host).Build()});
        auto& vm = f.interpreter;
        const auto call = [&](const VmObjectRef receiver, const std::string& name) {
            return f.CallStatic("LThrowableMessageProbe;", name,
                name == "inherited" ? "(LMessageException;)Ljava/lang/String;"
                                    : "(Ljava/lang/Throwable;)Ljava/lang/String;",
                {VmValue::Ref(receiver)});
        };
        const auto text = [&](const VmCallOutcome& outcome) {
            REQUIRE_MESSAGE(!outcome.exception.IsValid(), outcome.exception_message);
            return vm.StringUtf8(outcome.value.ref);
        };
        const auto base = vm.NewIntrinsicInstance("Ljava/lang/Exception;");
        const auto base_root = vm.ProtectReferences(std::array{base});
        for (const auto* message : std::array<const char*, 3>{nullptr, "", "message 中文"}) {
            const auto ref = message ? vm.NewStringUtf8(message) : VmObjectRef{};
            vm.SetThrowableMessage(base, ref);
            const auto localized = call(base, "localized");
            REQUIRE_FALSE(localized.exception.IsValid());
            CHECK(localized.value.ref == ref);
            CHECK(text(call(base, "render")) ==
                  std::string("java.lang.Exception") + (message ? std::string(": ") + message : ""));
        }
        const auto type = f.linker.ResolveDescriptor("LMessageException;");
        f.linker.EnsureClassLinked(type);
        const auto object = f.model.NewInstance(type, f.linker.Class(type).instance_slots);
        const auto object_root = vm.ProtectReferences(std::array{object});
        const auto message_field = f.linker.FindFieldRecursive(type, "message", "Ljava/lang/String;");
        const auto failure_field = f.linker.FindFieldRecursive(type, "failure", "Ljava/lang/Throwable;");
        REQUIRE(message_field.has_value());
        REQUIRE(failure_field.has_value());
        vm.SetThrowableMessage(object, vm.NewStringUtf8("stored message must not bypass override"));
        for (const auto* message : std::array<const char*, 3>{nullptr, "", "overridden 中文"}) {
            const auto ref = message ? vm.NewStringUtf8(message) : VmObjectRef{};
            f.model.InstanceSlots(object)[f.linker.Field(*message_field).slot] =
                Slot{ref.Value(), SlotTag::ref};
            const auto localized = call(object, "inherited");
            REQUIRE_FALSE(localized.exception.IsValid());
            CHECK(localized.value.ref == ref);
            CHECK(text(call(object, "render")) ==
                  std::string("MessageException") + (message ? std::string(": ") + message : ""));
        }
        const auto child_type = f.linker.ResolveDescriptor("LLocalizedException;");
        f.linker.EnsureClassLinked(child_type);
        const auto child = f.model.NewInstance(child_type, f.linker.Class(child_type).instance_slots);
        const auto child_root = vm.ProtectReferences(std::array{child});
        CHECK(text(call(child, "inherited")) == "localized");
        CHECK(text(call(child, "render")) == "LocalizedException: localized");

        const auto failure = vm.MakeThrowable("Ljava/lang/IllegalArgumentException;", "callback failure");
        f.model.InstanceSlots(object)[f.linker.Field(*failure_field).slot] =
            Slot{failure.Value(), SlotTag::ref};
        for (const auto* method : {"localized", "inherited", "render"}) {
            const auto outcome = call(object, method);
            CHECK(outcome.exception == failure);
            CHECK(outcome.exception_message == "callback failure");
        }
        CHECK(text(call(base, "render")) == "java.lang.Exception: message 中文");
    });
}

TEST_CASE("DVM-105 Throwable cause retains identity across GC and rejects overwrites") {
    Vm fixture;
    auto& vm = fixture.interpreter;
    const auto outer = vm.NewIntrinsicInstance("Ljava/lang/Exception;");
    const auto inner = vm.NewIntrinsicInstance("Ljava/lang/IllegalArgumentException;");
    const auto roots = vm.ProtectReferences(std::array{outer, inner});
    vm.InitializeThrowable(outer);
    vm.InitializeThrowable(inner);
    vm.InitThrowableCause(outer, inner);
    CHECK(vm.ThrowableCause(outer) == inner);
    static_cast<void>(vm.CollectGarbage("cause-test"));
    CHECK(vm.Model().ObjectClass(vm.ThrowableCause(outer)) == *fixture.linker.FindClass("Ljava/lang/IllegalArgumentException;"));
    CHECK_THROWS_AS(vm.InitThrowableCause(outer, VmObjectRef{}), VmJavaThrow);
    const auto self = vm.NewIntrinsicInstance("Ljava/lang/Exception;");
    vm.InitializeThrowable(self);
    CHECK_THROWS_AS(vm.InitThrowableCause(self, self), VmJavaThrow);
    vm.InitThrowableCause(self, VmObjectRef{});
    CHECK_THROWS_AS(vm.InitThrowableCause(self, inner), VmJavaThrow);
}

TEST_CASE("WU7-3 Throwable uses DEX source and the last debug position at the saved PC") {
    WithEachBackend([](InterpreterConfig config) {
        auto bytes = ReadFixture("interp.dex");
        const auto image = ogplay::loader::ParseDex(bytes);
        const auto classes = ogplay::loader::ReadDexClassData(bytes, image);
        const auto put32 = [&](std::size_t at, std::uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
        };
        const auto file = std::find_if(image.strings.begin(), image.strings.end(),
            [](const auto& text) { return text.value == u"hello"; });
        REQUIRE(file != image.strings.end());
        bool patched = false;
        for (const auto& data : classes) {
            const auto& def = image.classes.at(data.class_def_index);
            if (image.types.at(def.class_type_index).descriptor != "LThrowableMessageProbe;") continue;
            put32(image.header.class_defs_offset + data.class_def_index * 32 + 16,
                  static_cast<std::uint32_t>(file - image.strings.begin()));
            for (const auto& method : data.direct_methods) {
                if (image.strings.at(image.methods.at(method.method_index).name_string_index).value != u"create") continue;
                REQUIRE(method.code.has_value());
                put32(method.code->offset + 8, static_cast<std::uint32_t>(bytes.size()));
                // line_start=100, no parameters, emit (0,100), advance to
                // (3,95) with a changed file, emit, then END_SEQUENCE.
                // A later file state must not leak into an earlier saved PC.
                bytes.insert(bytes.end(), {100, 0, 14, 1, 3, 9, 1, 2, 0x7b, 14, 0});
                patched = true;
            }
        }
        REQUIRE(patched);
        put32(32, static_cast<std::uint32_t>(bytes.size()));
        put32(104, static_cast<std::uint32_t>(bytes.size()) - image.header.data_offset);
        DexClassLinker linker;
        linker.RegisterIntrinsics(CoreIntrinsicCatalog());
        ogplay::test::RegisterBootDex(linker);
        linker.RegisterDex(std::move(bytes));
        linker.Link();
        JniStringStore strings;
        JniPrimitiveArrayStore arrays;
        JavaObjectModel model(strings, arrays);
        ogplay::core::CapabilityLedger ledger;
        Interpreter vm(linker, model, nullptr, ledger, config);
        const auto type = linker.ResolveDescriptor("LThrowableMessageProbe;");
        const auto method = linker.FindDirectMethod(type, "create", "(Ljava/lang/String;)Ljava/lang/Throwable;");
        REQUIRE(method.has_value());
        const auto snapshot = model.NewPrimitiveArray(linker.ResolveDescriptor("[I"), JniPrimitiveKind::integer, 6);
        const auto root = vm.ProtectReferences(std::array{snapshot});
        const std::array pcs{0, 2, 3};
        for (int i = 0; i < 3; ++i) {
            model.SetPrimitiveElement(snapshot, i * 2, method->Value());
            model.SetPrimitiveElement(snapshot, i * 2 + 1, static_cast<std::uint64_t>(pcs.at(static_cast<std::size_t>(i))));
        }
        const auto trace = vm.MaterializeThrowableStack(snapshot);
        const auto trace_root = vm.ProtectReferences(std::array{trace});
        const auto element_type = linker.ResolveDescriptor("Ljava/lang/StackTraceElement;");
        const auto line = linker.FindVtableIndex(element_type, "getLineNumber", "()I");
        const auto file_name = linker.FindVtableIndex(element_type, "getFileName", "()Ljava/lang/String;");
        REQUIRE(line.has_value());
        REQUIRE(file_name.has_value());
        for (int i = 0; i < 3; ++i) {
            const auto element = model.GetObjectElement(trace, i);
            const std::array args{VmValue::Ref(element)};
            CHECK(vm.Call(linker.Class(element_type).vtable[*line], args).value.AsInt() == (i == 2 ? 95 : 100));
            const auto expected_file = i == 2 ? *ogplay::core::Utf16ToUtf8(
                image.strings.front().value, ogplay::core::InvalidUtf16Policy::replace) : "hello";
            CHECK(vm.StringUtf8(vm.Call(linker.Class(element_type).vtable[*file_name], args).value.ref) == expected_file);
        }
    });
}
