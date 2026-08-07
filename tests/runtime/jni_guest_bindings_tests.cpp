#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_bindings.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

struct GuestBindingsFixture final {
    GuestBindingsFixture()
        : bus(memory),
          cpu(bus),
          abi(memory),
          dispatcher(ledger),
          invocations(classes),
          java_vm(environment) {
        memory.Map({output, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        ogplay::runtime::BindJniGuestCoreSlots(
            dispatcher, environment, classes, invocations, strings, arrays,
            java_vm, memory);
    }

    [[nodiscard]] std::uint32_t CallEnvironment(
        const std::string_view name, const std::uint64_t thread_id,
        const std::uint32_t r1 = 0U, const std::uint32_t r2 = 0U,
        const std::uint32_t r3 = 0U,
        const std::uint32_t stack_word = 0U) {
        return Call(false, ogplay::runtime::FindJniSlot(name)->Value(),
                    thread_id, r1, r2, r3, stack_word);
    }

    [[nodiscard]] std::uint32_t CallJavaVm(
        const std::string_view name, const std::uint64_t thread_id,
        const std::uint32_t r1 = 0U, const std::uint32_t r2 = 0U,
        const std::uint32_t r3 = 0U) {
        return Call(true,
                    ogplay::runtime::FindJniInvokeSlot(name)->Value(),
                    thread_id, r1, r2, r3);
    }

    [[nodiscard]] std::uint32_t ReadOutput() {
        return bus.Read32(output);
    }

    void WriteString(const std::uint32_t offset,
                     const std::string_view value) {
        std::vector<std::byte> bytes;
        bytes.reserve(value.size() + 1);
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        memory.Write(output.Add(offset), bytes);
    }

    void Seal() { dispatcher.Seal(); }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::runtime::GuestJniAbi abi;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniGuestCallDispatcher dispatcher;
    ogplay::runtime::JniEnvironment environment;
    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations;
    ogplay::runtime::JniStringStore strings;
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    ogplay::runtime::JniJavaVm java_vm;
    const ogplay::memory::GuestAddress output{0x72000000U};

private:
    [[nodiscard]] std::uint32_t Call(
        const bool java_vm_call, const std::uint16_t slot,
        const std::uint64_t thread_id, const std::uint32_t r1,
        const std::uint32_t r2, const std::uint32_t r3,
        const std::uint32_t stack_word = 0U) {
        const auto table =
            java_vm_call ? ogplay::runtime::kJniGuestInvokeTable
                         : ogplay::runtime::kJniGuestEnvironmentTable;
        const auto target =
            bus.Read32(table.Add(slot * sizeof(std::uint32_t)));
        ogplay::cpu::A32State state;
        state.SetState(ogplay::cpu::ExecutionState::thumb);
        state.SetThreadId(thread_id);
        state.SetRegister(ogplay::cpu::CoreRegister::pc,
                          target & ~1U);
        state.SetRegister(
            ogplay::cpu::CoreRegister::r0,
            java_vm_call ? abi.JavaVm().Value()
                         : abi.Environment().Value());
        state.SetRegister(ogplay::cpu::CoreRegister::r1, r1);
        state.SetRegister(ogplay::cpu::CoreRegister::r2, r2);
        state.SetRegister(ogplay::cpu::CoreRegister::r3, r3);
        const auto stack_pointer = output.Add(0x800U);
        bus.Write32(stack_pointer, stack_word, thread_id);
        state.SetRegister(ogplay::cpu::CoreRegister::sp,
                          stack_pointer.Value());
        state.SetRegister(ogplay::cpu::CoreRegister::lr,
                          0x12345679U);
        cpu.SetState(state);
        const auto stopped = cpu.Run(1);
        if (!dispatcher.Handle(cpu, stopped)) {
            throw std::runtime_error("test JNI guest trap was not handled");
        }
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }
};

[[nodiscard]] std::uint32_t StatusWord(
    const ogplay::runtime::JniStatus status) {
    return std::bit_cast<std::uint32_t>(
        static_cast<ogplay::runtime::JniInt>(status));
}

}  // namespace

TEST_CASE("guest JNI core bindings cover exact behavior-backed slots") {
    GuestBindingsFixture fixture;
    constexpr std::array<std::string_view, 21> environment_names{
        "GetVersion",          "Throw",
        "ExceptionOccurred",   "ExceptionClear",
        "PushLocalFrame",      "PopLocalFrame",
        "NewGlobalRef",        "DeleteGlobalRef",
        "DeleteLocalRef",      "IsSameObject",
        "NewLocalRef",         "EnsureLocalCapacity",
        "GetJavaVM",           "NewWeakGlobalRef",
        "DeleteWeakGlobalRef", "ExceptionCheck",
        "GetStaticMethodID",   "CallStaticObjectMethod",
        "GetArrayLength",      "GetByteArrayRegion",
        "NewStringUTF"};
    for (const auto name : environment_names) {
        CAPTURE(name);
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *ogplay::runtime::FindJniSlot(name)));
    }
    constexpr std::array<std::string_view, 4> java_vm_names{
        "AttachCurrentThread", "DetachCurrentThread", "GetEnv",
        "AttachCurrentThreadAsDaemon"};
    for (const auto name : java_vm_names) {
        CAPTURE(name);
        CHECK(fixture.dispatcher.IsJavaVmBound(
            *ogplay::runtime::FindJniInvokeSlot(name)));
    }
    CHECK_FALSE(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("FindClass")));
    fixture.Seal();
}

TEST_CASE("guest JNI NewStringUTF publishes a decoded local string") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        402U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const std::string encoded{"A\xC0\x80"
                              "B",
                              4};
    fixture.WriteString(0x100U, encoded);
    fixture.Seal();

    const auto reference = ogplay::runtime::JniReference{
        fixture.CallEnvironment(
            "NewStringUTF", 402U, fixture.output.Add(0x100U).Value())};
    REQUIRE_FALSE(reference.IsNull());
    const auto identity =
        fixture.environment.ResolveObjectForHle(402U, reference);
    REQUIRE(identity.has_value());
    CHECK(fixture.strings.Region(*identity, 0, 3) ==
          std::vector<ogplay::runtime::JniChar>{'A', 0, 'B'});
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            fixture.CallEnvironment("NewStringUTF", 402U, 0U)),
        "JNI guest modified UTF-8 pointer is null",
        ogplay::runtime::JniGuestBindingError);
}

TEST_CASE("guest JNI array length resolves the unified primitive array") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        404U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto array_identity = fixture.arrays.New(
        ogplay::runtime::JniPrimitiveKind::byte, 7);
    const auto array =
        fixture.environment.PublishLocalObject(404U, array_identity);
    fixture.Seal();

    CHECK(fixture.CallEnvironment(
              "GetArrayLength", 404U, array.Value()) == 7U);
    CHECK_THROWS_AS(
        static_cast<void>(
            fixture.CallEnvironment("GetArrayLength", 404U, 0U)),
        ogplay::runtime::JniGuestBindingError);
}

TEST_CASE("guest JNI byte array region copies checked bytes to guest memory") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        405U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);

    const auto array_identity = fixture.arrays.New(
        ogplay::runtime::JniPrimitiveKind::byte, 4);
    fixture.arrays.SetRegion(
        array_identity, 0,
        ogplay::runtime::JniPrimitiveArrayData{
            std::vector<ogplay::runtime::JniByte>{0x11, -2, 0x7f, -128}});
    const auto array =
        fixture.environment.PublishLocalObject(405U, array_identity);

    const auto integer_identity = fixture.arrays.New(
        ogplay::runtime::JniPrimitiveKind::integer, 1);
    const auto integer_array =
        fixture.environment.PublishLocalObject(405U, integer_identity);
    const auto destination = fixture.output.Add(0x300U);
    fixture.Seal();

    static_cast<void>(fixture.CallEnvironment(
        "GetByteArrayRegion", 405U, array.Value(), 1U, 2U,
        destination.Value()));
    CHECK(fixture.bus.Read8(destination) == 0xfeU);
    CHECK(fixture.bus.Read8(destination.Add(1U)) == 0x7fU);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetByteArrayRegion", 405U, 0U, 0U, 1U,
            destination.Value())),
        "GetByteArrayRegion requires a valid array reference",
        ogplay::runtime::JniGuestBindingError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetByteArrayRegion", 405U, integer_array.Value(), 0U, 1U,
            destination.Value())),
        "GetByteArrayRegion requires a byte array reference",
        ogplay::runtime::JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetByteArrayRegion", 405U, array.Value(),
            std::bit_cast<std::uint32_t>(ogplay::runtime::JniInt{-1}), 1U,
            destination.Value())),
        ogplay::runtime::JniArrayError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetByteArrayRegion", 405U, array.Value(), 0U, 1U, 0U)),
        "GetByteArrayRegion requires a non-null output buffer",
        ogplay::runtime::JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetByteArrayRegion", 405U, array.Value(), 0U, 1U,
            ogplay::runtime::kJniGuestEnvironmentTable.Value())),
        ogplay::memory::MemoryFault);
}

TEST_CASE("guest JNI static object call decodes descriptor-backed variadics") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        403U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto identity = fixture.classes.RegisterClass(
        {"fixture/Resources",
         {},
         {{"load", "(Ljava/lang/String;)[B", "resource.load", true}},
         {}});
    const auto method = fixture.classes.GetMethodId(
        identity, "load", "(Ljava/lang/String;)[B", true);
    REQUIRE(method.has_value());
    const auto java_class =
        fixture.environment.PublishLocalObject(403U, identity);
    const auto argument = fixture.environment.PublishLocalObject(
        403U, ogplay::runtime::AllocateJniHostObjectIdentity());
    const auto expected = fixture.environment.PublishLocalObject(
        403U, ogplay::runtime::AllocateJniHostObjectIdentity());
    fixture.invocations.RegisterHandler(
        "resource.load",
        [argument, expected](const ogplay::runtime::JniInvocation& invocation) {
            CHECK(invocation.kind ==
                  ogplay::runtime::JniInvocationKind::static_method);
            CHECK(invocation.argument_source ==
                  ogplay::runtime::JniArgumentSource::variadic);
            REQUIRE(invocation.arguments.size() == 1);
            CHECK(std::get<ogplay::runtime::JniReference>(
                      invocation.arguments[0]) == argument);
            return ogplay::runtime::JniValue{expected};
        });
    fixture.Seal();

    CHECK(fixture.CallEnvironment(
              "CallStaticObjectMethod", 403U, java_class.Value(),
              method->Value(), argument.Value()) == expected.Value());
}

TEST_CASE("guest JNI static method lookup uses declared class identity") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        401U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto identity = fixture.classes.RegisterClass(
        {"fixture/Resource",
         {},
         {{"load", "(I)[B", "resource.load", true},
          {"reset", "()V", "resource.reset", false}},
         {}});
    const auto java_class =
        fixture.environment.PublishLocalObject(401U, identity);
    fixture.WriteString(0x100U, "load");
    fixture.WriteString(0x120U, "(I)[B");
    fixture.WriteString(0x140U, "reset");
    fixture.WriteString(0x160U, "()V");
    fixture.Seal();

    const auto method = fixture.CallEnvironment(
        "GetStaticMethodID", 401U, java_class.Value(),
        fixture.output.Add(0x100U).Value(),
        fixture.output.Add(0x120U).Value());
    CHECK(method == fixture.classes.GetMethodId(
                        identity, "load", "(I)[B", true)
                        ->Value());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "GetStaticMethodID", 401U, java_class.Value(),
            fixture.output.Add(0x140U).Value(),
            fixture.output.Add(0x160U).Value())),
        "JNI guest static method is not declared: reset()V",
        ogplay::runtime::JniGuestBindingError);
}

TEST_CASE("guest JNI core bindings execute references exceptions and VM export") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        201U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    fixture.Seal();

    CHECK(fixture.CallEnvironment("GetVersion", 201U) ==
          static_cast<std::uint32_t>(
              ogplay::runtime::kJniVersion1_6));
    CHECK(fixture.CallEnvironment(
              "GetJavaVM", 201U, fixture.output.Value()) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK(fixture.ReadOutput() ==
          ogplay::runtime::kJniGuestJavaVm.Value());

    const auto local = fixture.environment.PublishLocalObject(
        201U, {ogplay::runtime::JniObjectDomain::host, 0x9001U});
    const auto global = fixture.CallEnvironment(
        "NewGlobalRef", 201U, local.Value());
    CHECK(global != 0U);
    CHECK(fixture.CallEnvironment(
              "IsSameObject", 201U, local.Value(), global) == 1U);
    static_cast<void>(fixture.CallEnvironment(
        "DeleteGlobalRef", 201U, global));

    const auto throwable = fixture.environment.PublishLocalObject(
        201U, {ogplay::runtime::JniObjectDomain::host, 0x9002U});
    CHECK(fixture.CallEnvironment(
              "Throw", 201U, throwable.Value()) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK(fixture.CallEnvironment("ExceptionCheck", 201U) == 1U);
    CHECK(fixture.CallEnvironment(
              "ExceptionOccurred", 201U) != 0U);
    static_cast<void>(
        fixture.CallEnvironment("ExceptionClear", 201U));
    CHECK(fixture.CallEnvironment("ExceptionCheck", 201U) == 0U);
}

TEST_CASE("guest JavaVM bindings attach query detach and reject unknown args") {
    GuestBindingsFixture fixture;
    fixture.Seal();

    CHECK(fixture.CallJavaVm(
              "GetEnv", 301U, fixture.output.Value(),
              static_cast<std::uint32_t>(
                  ogplay::runtime::kJniVersion1_6)) ==
          StatusWord(ogplay::runtime::JniStatus::detached));
    CHECK(fixture.ReadOutput() == 0U);

    CHECK(fixture.CallJavaVm(
              "AttachCurrentThread", 301U,
              fixture.output.Value()) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK(fixture.ReadOutput() ==
          ogplay::runtime::kJniGuestEnvironment.Value());
    CHECK(fixture.environment.IsThreadAttached(301U));
    CHECK(fixture.CallJavaVm(
              "GetEnv", 301U, fixture.output.Value(),
              static_cast<std::uint32_t>(
                  ogplay::runtime::kJniVersion1_6)) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK(fixture.CallJavaVm("DetachCurrentThread", 301U) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK_FALSE(fixture.environment.IsThreadAttached(301U));

    CHECK(fixture.CallJavaVm(
              "AttachCurrentThreadAsDaemon", 302U,
              fixture.output.Value()) ==
          StatusWord(ogplay::runtime::JniStatus::ok));
    CHECK(fixture.java_vm.IsDaemon(302U));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallJavaVm(
            "AttachCurrentThread", 303U, fixture.output.Value(),
            0x72000100U)),
        "non-null JavaVM attach arguments are not implemented",
        ogplay::runtime::JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.CallJavaVm(
            "AttachCurrentThread", 304U, 0x73000000U)),
        ogplay::memory::MemoryFault);
    CHECK_FALSE(fixture.environment.IsThreadAttached(304U));
}
