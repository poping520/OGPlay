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
#include "ogplay/runtime/integration/jni_guest_static_calls.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

struct GuestBindingsFixture final {
    GuestBindingsFixture()
        : bus(memory),
          cpu(bus),
          abi(memory),
          dispatcher(ledger),
          invocations(classes), fields(classes), objects(classes),
          java_vm(environment) {
        memory.Map({output, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        ogplay::runtime::JniGuestBindingContext context{
            environment, classes, invocations, fields, strings, arrays,
            java_vm, objects, memory, &natives};
        ogplay::runtime::BindJniGuestSlots(dispatcher, context);
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

    [[nodiscard]] std::uint32_t ReturnHighWord() const {
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r1);
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
    ogplay::runtime::JniFieldStore fields;
    ogplay::runtime::JniGuestObjectRegistry objects;
    ogplay::runtime::JniStringStore strings;
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    ogplay::runtime::JniNativeRegistry natives;
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

TEST_CASE("guest JNI aggregate binds the exact behavior-backed slot sets") {
    GuestBindingsFixture fixture;
    constexpr std::string_view environment_names[]{
        "GetVersion",          "Throw",
        "ExceptionOccurred",   "ExceptionClear",
        "PushLocalFrame",      "PopLocalFrame",
        "NewGlobalRef",        "DeleteGlobalRef",
        "DeleteLocalRef",      "IsSameObject",
        "NewLocalRef",         "EnsureLocalCapacity",
        "GetJavaVM",           "NewWeakGlobalRef",
        "DeleteWeakGlobalRef", "ExceptionCheck",
        "GetStaticMethodID",   "CallStaticObjectMethod",
        "CallStaticObjectMethodV", "CallStaticObjectMethodA",
        "CallStaticBooleanMethod", "CallStaticBooleanMethodV",
        "CallStaticBooleanMethodA", "CallStaticByteMethod",
        "CallStaticByteMethodV", "CallStaticByteMethodA",
        "CallStaticCharMethod", "CallStaticCharMethodV",
        "CallStaticCharMethodA", "CallStaticShortMethod",
        "CallStaticShortMethodV", "CallStaticShortMethodA",
        "CallStaticIntMethod", "CallStaticIntMethodV",
        "CallStaticIntMethodA", "CallStaticLongMethod",
        "CallStaticLongMethodV", "CallStaticLongMethodA",
        "CallStaticFloatMethod", "CallStaticFloatMethodV",
        "CallStaticFloatMethodA", "CallStaticDoubleMethod",
        "CallStaticDoubleMethodV", "CallStaticDoubleMethodA",
        "CallStaticVoidMethod", "CallStaticVoidMethodV",
        "CallStaticVoidMethodA",
        "GetArrayLength",      "GetByteArrayRegion",
        "NewStringUTF",        "GetStringUTFLength",
        "GetStringUTFChars",   "ReleaseStringUTFChars",
        "GetStringUTFRegion"};
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
    constexpr std::string_view class_object_names[]{
        "FindClass", "GetMethodID", "GetObjectClass", "IsInstanceOf",
        "NewObject", "NewObjectV", "NewObjectA"};
    for (const auto name : class_object_names) {
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *ogplay::runtime::FindJniSlot(name)));
    }
    constexpr std::string_view instance_types[]{
        "Object", "Boolean", "Byte", "Char", "Short", "Int", "Long",
        "Float", "Double", "Void"};
    constexpr std::string_view call_variants[]{"", "V", "A"};
    for (const auto type : instance_types) {
        for (const auto variant : call_variants) {
            CHECK(fixture.dispatcher.IsEnvironmentBound(
                *ogplay::runtime::FindJniSlot(
                    std::string("Call") + std::string(type) + "Method" +
                    std::string(variant))));
        }
    }
    constexpr std::string_view field_types[]{
        "Object", "Boolean", "Byte", "Char", "Short", "Int", "Long",
        "Float", "Double"};
    CHECK(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("GetStaticFieldID")));
    CHECK(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("RegisterNatives")));
    CHECK(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("UnregisterNatives")));
    CHECK(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("GetFieldID")));
    constexpr std::string_view utf16_names[]{
        "NewString", "GetStringLength", "GetStringChars",
        "ReleaseStringChars", "GetStringRegion"};
    for (const auto name : utf16_names) {
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *ogplay::runtime::FindJniSlot(name)));
    }
    for (const auto type : field_types) {
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *ogplay::runtime::FindJniSlot(
                std::string("GetStatic") + std::string(type) + "Field")));
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *ogplay::runtime::FindJniSlot(
                std::string("SetStatic") + std::string(type) + "Field")));
    }

    std::size_t environment_count{};
    for (std::size_t index = ogplay::runtime::kJniReservedSlotCount;
         index < ogplay::runtime::kJniNativeInterfaceSlotCount; ++index) {
        environment_count += fixture.dispatcher.IsEnvironmentBound(
            ogplay::runtime::JniSlot{static_cast<std::uint16_t>(index)})
                                 ? 1U
                                 : 0U;
    }
    CHECK(environment_count == 136U);
    std::size_t java_vm_count{};
    for (std::size_t index = 3U;
         index < ogplay::runtime::kJniInvokeInterfaceSlotCount; ++index) {
        java_vm_count += fixture.dispatcher.IsJavaVmBound(
            ogplay::runtime::JniInvokeSlot{
                static_cast<std::uint8_t>(index)})
                             ? 1U
                             : 0U;
    }
    CHECK(java_vm_count == 4U);
    CHECK_FALSE(fixture.dispatcher.IsEnvironmentBound(
        *ogplay::runtime::FindJniSlot("GetStringCritical")));
    fixture.Seal();
    CHECK(fixture.dispatcher.IsSealed());
    CHECK_THROWS_WITH_AS(
        fixture.dispatcher.BindEnvironment(
            *ogplay::runtime::FindJniSlot("GetFieldID"),
            [](const ogplay::runtime::JniGuestCallFrame&) {
                return ogplay::runtime::JniGuestCallResult{};
            }),
        "JNI guest dispatcher is sealed", std::logic_error);
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

TEST_CASE("guest JNI modified UTF string family owns checked guest leases") {
    GuestBindingsFixture fixture;
    constexpr std::uint64_t thread_id = 403U;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        thread_id, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const std::array<std::uint8_t, 4> encoded{'A', 0xc0U, 0x80U, 'B'};
    const auto identity = fixture.strings.CreateModifiedUtf8(encoded);
    const auto string = fixture.environment.PublishLocalObject(
        thread_id, identity);
    fixture.Seal();

    CHECK(fixture.CallEnvironment(
              "GetStringUTFLength", thread_id, string.Value()) == 4U);
    const auto copy_flag = fixture.output.Add(0x200U);
    const auto pointer = ogplay::memory::GuestAddress{
        fixture.CallEnvironment(
            "GetStringUTFChars", thread_id, string.Value(),
            copy_flag.Value())};
    REQUIRE_FALSE(pointer.IsNull());
    std::array<std::byte, 5> leased{};
    fixture.memory.Read(pointer, leased, thread_id);
    CHECK(leased == std::array<std::byte, 5>{
                        std::byte{'A'}, std::byte{0xc0}, std::byte{0x80},
                        std::byte{'B'}, std::byte{0}});
    std::byte copied{};
    fixture.memory.Read(copy_flag, std::span{&copied, 1}, thread_id);
    CHECK(copied == std::byte{1});

    const auto region = fixture.output.Add(0x300U);
    static_cast<void>(fixture.CallEnvironment(
        "GetStringUTFRegion", thread_id, string.Value(), 0U, 3U,
        region.Value()));
    std::array<std::byte, 4> region_bytes{};
    fixture.memory.Read(region, region_bytes, thread_id);
    CHECK(region_bytes == std::array<std::byte, 4>{
                              std::byte{'A'}, std::byte{0xc0},
                              std::byte{0x80}, std::byte{'B'}});

    static_cast<void>(fixture.CallEnvironment(
        "ReleaseStringUTFChars", thread_id, string.Value(),
        pointer.Value()));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "ReleaseStringUTFChars", thread_id, string.Value(),
            pointer.Value())),
        "ReleaseStringUTFChars pointer does not match an active lease",
        ogplay::runtime::JniGuestBindingError);
    CHECK(fixture.CallEnvironment(
              "GetStringUTFChars", thread_id, string.Value()) != 0U);
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

TEST_CASE("guest JNI static int call preserves signed variadic result") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        407U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto identity = fixture.classes.RegisterClass(
        {"fixture/Resources",
         {},
         {{"length", "(Ljava/lang/String;)I", "resource.length", true},
          {"load", "()[B", "resource.load", true}},
         {}});
    const auto method = fixture.classes.GetMethodId(
        identity, "length", "(Ljava/lang/String;)I", true);
    const auto object_method = fixture.classes.GetMethodId(
        identity, "load", "()[B", true);
    REQUIRE(method.has_value());
    REQUIRE(object_method.has_value());
    const auto java_class =
        fixture.environment.PublishLocalObject(407U, identity);
    const auto argument = fixture.environment.PublishLocalObject(
        407U, ogplay::runtime::AllocateJniHostObjectIdentity());
    fixture.invocations.RegisterHandler(
        "resource.length",
        [argument](const ogplay::runtime::JniInvocation& invocation) {
            CHECK(invocation.kind ==
                  ogplay::runtime::JniInvocationKind::static_method);
            CHECK(invocation.argument_source ==
                  ogplay::runtime::JniArgumentSource::variadic);
            REQUIRE(invocation.arguments.size() == 1);
            CHECK(std::get<ogplay::runtime::JniReference>(
                      invocation.arguments[0]) == argument);
            return ogplay::runtime::JniValue{ogplay::runtime::JniInt{-17}};
        });
    fixture.Seal();

    CHECK(fixture.CallEnvironment(
              "CallStaticIntMethod", 407U, java_class.Value(),
              method->Value(), argument.Value()) ==
          std::bit_cast<std::uint32_t>(ogplay::runtime::JniInt{-17}));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "CallStaticIntMethod", 407U, java_class.Value(),
            object_method->Value())),
        "CallStaticIntMethod return type does not match method descriptor",
        ogplay::runtime::JniGuestBindingError);
}

TEST_CASE("guest JNI static call family encodes every return ABI") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        408U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto identity = fixture.classes.RegisterClass(
        {"fixture/StaticReturns",
         {},
         {{"object", "()Ljava/lang/Object;", "return.object", true},
          {"boolean", "()Z", "return.boolean", true},
          {"byte", "()B", "return.byte", true},
          {"character", "()C", "return.character", true},
          {"short", "()S", "return.short", true},
          {"integer", "()I", "return.integer", true},
          {"long", "()J", "return.long", true},
          {"float", "()F", "return.float", true},
          {"double", "()D", "return.double", true},
          {"void", "()V", "return.void", true}},
         {}});
    const auto java_class =
        fixture.environment.PublishLocalObject(408U, identity);
    const auto object = fixture.environment.PublishLocalObject(
        408U, ogplay::runtime::AllocateJniHostObjectIdentity());
    fixture.invocations.RegisterHandler(
        "return.object", [object](const auto&) {
            return ogplay::runtime::JniValue{object};
        });
    fixture.invocations.RegisterHandler(
        "return.boolean", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniBoolean{1}};
        });
    fixture.invocations.RegisterHandler(
        "return.byte", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniByte{-2}};
        });
    fixture.invocations.RegisterHandler(
        "return.character", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniChar{0xff01}};
        });
    fixture.invocations.RegisterHandler(
        "return.short", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniShort{-300}};
        });
    fixture.invocations.RegisterHandler(
        "return.integer", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniInt{-17}};
        });
    fixture.invocations.RegisterHandler(
        "return.long", [](const auto&) {
            return ogplay::runtime::JniValue{
                ogplay::runtime::JniLong{0x1122334455667788LL}};
        });
    fixture.invocations.RegisterHandler(
        "return.float", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniFloat{1.5F}};
        });
    fixture.invocations.RegisterHandler(
        "return.double", [](const auto&) {
            return ogplay::runtime::JniValue{ogplay::runtime::JniDouble{-2.25}};
        });
    fixture.invocations.RegisterHandler(
        "return.void", [](const auto&) {
            return ogplay::runtime::JniValue{std::monostate{}};
        });
    fixture.Seal();

    struct Expected final {
        std::string_view suffix;
        std::string_view method;
        std::string_view descriptor;
        std::uint32_t low;
        std::uint32_t high;
    };
    constexpr std::array expected{
        Expected{"Object", "object", "()Ljava/lang/Object;", 0U, 0U},
        Expected{"Boolean", "boolean", "()Z", 1U, 0U},
        Expected{"Byte", "byte", "()B", 0xfffffffeU, 0U},
        Expected{"Char", "character", "()C", 0xff01U, 0U},
        Expected{"Short", "short", "()S", 0xfffffed4U, 0U},
        Expected{"Int", "integer", "()I", 0xffffffefU, 0U},
        Expected{"Long", "long", "()J", 0x55667788U, 0x11223344U},
        Expected{"Float", "float", "()F", 0x3fc00000U, 0U},
        Expected{"Double", "double", "()D", 0U, 0xc0020000U},
        Expected{"Void", "void", "()V",
                 ogplay::runtime::kJniGuestEnvironment.Value(), 0U},
    };
    for (const auto& item : expected) {
        const auto method = fixture.classes.GetMethodId(
            identity, std::string{item.method}, std::string{item.descriptor},
            true);
        REQUIRE(method.has_value());
        for (const auto variant : {std::string_view{""},
                                   std::string_view{"V"},
                                   std::string_view{"A"}}) {
            const auto name = std::string{"CallStatic"} +
                              std::string{item.suffix} + "Method" +
                              std::string{variant};
            CAPTURE(name);
            const auto low = fixture.CallEnvironment(
                name, 408U, java_class.Value(), method->Value());
            CHECK(low == (item.suffix == "Object" ? object.Value()
                                                   : item.low));
            if (item.suffix == "Long" || item.suffix == "Double") {
                CHECK(fixture.ReturnHighWord() == item.high);
            }
        }
    }
}

TEST_CASE("guest JNI static V and A calls decode their distinct A32 layouts") {
    GuestBindingsFixture fixture;
    const auto attached = fixture.java_vm.AttachCurrentThread(
        409U, ogplay::runtime::kJniVersion1_6);
    REQUIRE(attached.status == ogplay::runtime::JniStatus::ok);
    const auto identity = fixture.classes.RegisterClass(
        {"fixture/StaticArguments",
         {},
         {{"mix", "(IJFDLjava/lang/Object;)D", "arguments.mix", true}},
         {}});
    const auto method = fixture.classes.GetMethodId(
        identity, "mix", "(IJFDLjava/lang/Object;)D", true);
    REQUIRE(method.has_value());
    const auto java_class =
        fixture.environment.PublishLocalObject(409U, identity);
    const auto object = fixture.environment.PublishLocalObject(
        409U, ogplay::runtime::AllocateJniHostObjectIdentity());
    fixture.invocations.RegisterHandler(
        "arguments.mix", [object](const ogplay::runtime::JniInvocation& call) {
            REQUIRE(call.arguments.size() == 5U);
            CHECK(std::get<ogplay::runtime::JniInt>(call.arguments[0]) == -7);
            CHECK(std::get<ogplay::runtime::JniLong>(call.arguments[1]) ==
                  0x1122334455667788LL);
            CHECK(std::get<ogplay::runtime::JniFloat>(call.arguments[2]) ==
                  doctest::Approx(1.25F));
            CHECK(std::get<ogplay::runtime::JniDouble>(call.arguments[3]) ==
                  doctest::Approx(-9.5));
            CHECK(std::get<ogplay::runtime::JniReference>(call.arguments[4]) ==
                  object);
            return ogplay::runtime::JniValue{
                call.argument_source == ogplay::runtime::JniArgumentSource::va_list
                    ? ogplay::runtime::JniDouble{12.5}
                    : ogplay::runtime::JniDouble{-3.5}};
        });
    const auto write64 = [&fixture](const ogplay::memory::GuestAddress address,
                                    const std::uint64_t value) {
        fixture.bus.Write32(address, static_cast<std::uint32_t>(value));
        fixture.bus.Write32(address.Add(4U),
                            static_cast<std::uint32_t>(value >> 32U));
    };
    const auto va_list = fixture.output.Add(0x400U);
    fixture.bus.Write32(va_list,
                        std::bit_cast<std::uint32_t>(
                            ogplay::runtime::JniInt{-7}));
    write64(va_list.Add(8U), 0x1122334455667788ULL);
    write64(va_list.Add(16U),
            std::bit_cast<std::uint64_t>(
                ogplay::runtime::JniDouble{1.25}));
    write64(va_list.Add(24U),
            std::bit_cast<std::uint64_t>(
                ogplay::runtime::JniDouble{-9.5}));
    fixture.bus.Write32(va_list.Add(32U), object.Value());

    const auto values = fixture.output.Add(0x500U);
    fixture.bus.Write32(values,
                        std::bit_cast<std::uint32_t>(
                            ogplay::runtime::JniInt{-7}));
    write64(values.Add(8U), 0x1122334455667788ULL);
    fixture.bus.Write32(values.Add(16U),
                        std::bit_cast<std::uint32_t>(
                            ogplay::runtime::JniFloat{1.25F}));
    write64(values.Add(24U),
            std::bit_cast<std::uint64_t>(
                ogplay::runtime::JniDouble{-9.5}));
    fixture.bus.Write32(values.Add(32U), object.Value());
    fixture.Seal();

    CHECK(fixture.CallEnvironment(
              "CallStaticDoubleMethodV", 409U, java_class.Value(),
              method->Value(), va_list.Value()) == 0U);
    CHECK(fixture.ReturnHighWord() == 0x40290000U);
    CHECK(fixture.CallEnvironment(
              "CallStaticDoubleMethodA", 409U, java_class.Value(),
              method->Value(), values.Value()) == 0U);
    CHECK(fixture.ReturnHighWord() == 0xc00c0000U);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "CallStaticDoubleMethodV", 409U, java_class.Value(),
            method->Value(), 0U)),
        "JNI guest va_list pointer is null",
        ogplay::runtime::JniGuestBindingError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.CallEnvironment(
            "CallStaticDoubleMethodA", 409U, java_class.Value(),
            method->Value(), 0U)),
        "JNI guest jvalue array pointer is null",
        ogplay::runtime::JniGuestBindingError);
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
