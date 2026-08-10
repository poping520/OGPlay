#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
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
#include "ogplay/runtime/integration/jni_guest_static_fields.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

class InstanceFixture final {
public:
    InstanceFixture()
        : bus(memory), cpu(bus), abi(memory), dispatcher(ledger),
          invocations(classes), fields(classes), objects(classes),
          java_vm(environment) {
        memory.Map({output, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        ogplay::runtime::BindJniGuestCoreSlots(
            dispatcher, environment, classes, invocations, strings, arrays,
            java_vm, memory, &objects);
        ogplay::runtime::BindJniGuestStaticFieldSlots(
            dispatcher, environment, classes, fields, memory);
    }

    void Write64(const std::uint64_t value) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(
                (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
        }
        memory.Write(output.Add(0x800U), bytes);
    }

    [[nodiscard]] std::array<std::uint32_t, 2> CallPair(
        const std::string_view name, const std::uint32_t r1 = 0,
        const std::uint32_t r2 = 0, const std::uint32_t r3 = 0) {
        const auto slot = ogplay::runtime::FindJniSlot(name);
        REQUIRE(slot.has_value());
        const auto target = bus.Read32(
            ogplay::runtime::kJniGuestEnvironmentTable.Add(
                slot->Value() * sizeof(std::uint32_t)));
        ogplay::cpu::A32State state;
        state.SetState(ogplay::cpu::ExecutionState::thumb);
        state.SetThreadId(thread_id);
        state.SetRegister(ogplay::cpu::CoreRegister::pc, target & ~1U);
        state.SetRegister(ogplay::cpu::CoreRegister::r0,
                          abi.Environment().Value());
        state.SetRegister(ogplay::cpu::CoreRegister::r1, r1);
        state.SetRegister(ogplay::cpu::CoreRegister::r2, r2);
        state.SetRegister(ogplay::cpu::CoreRegister::r3, r3);
        state.SetRegister(ogplay::cpu::CoreRegister::sp,
                          output.Add(0x800U).Value());
        state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x12345679U);
        cpu.SetState(state);
        const auto stopped = cpu.Run(1);
        if (!dispatcher.Handle(cpu, stopped)) {
            throw std::runtime_error("guest JNI dispatcher rejected thunk");
        }
        const auto result = cpu.GetState();
        return {result.Register(ogplay::cpu::CoreRegister::r0),
                result.Register(ogplay::cpu::CoreRegister::r1)};
    }

    void WriteString(const std::uint32_t offset,
                     const std::string_view value) {
        std::vector<std::byte> bytes;
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        memory.Write(output.Add(offset), bytes);
    }

    [[nodiscard]] std::uint32_t Call(
        const std::string_view name, const std::uint32_t r1 = 0,
        const std::uint32_t r2 = 0, const std::uint32_t r3 = 0) {
        return CallPair(name, r1, r2, r3)[0];
    }

    static constexpr std::uint64_t thread_id = 501U;
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
    ogplay::runtime::JniJavaVm java_vm;
    const ogplay::memory::GuestAddress output{0x72000000U};
};

}  // namespace

TEST_CASE("guest JNI class object and instance call family dispatches exact methods") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto java_class = fixture.classes.RegisterClass(
        {"fixture/Counter", {},
         {{"<init>", "(I)V", "counter.construct", false},
          {"value", "()I", "counter.value", false}}, {}});
    JniInt constructed{};
    fixture.invocations.RegisterHandler(
        "counter.construct",
        [&constructed](const JniInvocation& invocation) {
            constructed = std::get<JniInt>(invocation.arguments[0]);
            return JniValue{std::monostate{}};
        });
    fixture.invocations.RegisterHandler(
        "counter.value",
        [&constructed](const JniInvocation&) {
            return JniValue{constructed + 35};
        });
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    fixture.WriteString(0x100U, "fixture/Counter");
    fixture.WriteString(0x140U, "<init>");
    fixture.WriteString(0x160U, "(I)V");
    fixture.WriteString(0x180U, "value");
    fixture.WriteString(0x1a0U, "()I");
    fixture.dispatcher.Seal();

    const auto class_reference = JniReference{fixture.Call(
        "FindClass", fixture.output.Add(0x100U).Value())};
    REQUIRE_FALSE(class_reference.IsNull());
    const auto constructor = fixture.Call(
        "GetMethodID", class_reference.Value(),
        fixture.output.Add(0x140U).Value(),
        fixture.output.Add(0x160U).Value());
    const auto value = fixture.Call(
        "GetMethodID", class_reference.Value(),
        fixture.output.Add(0x180U).Value(),
        fixture.output.Add(0x1a0U).Value());
    const auto object = JniReference{fixture.Call(
        "NewObject", class_reference.Value(), constructor, 7U)};
    REQUIRE_FALSE(object.IsNull());
    CHECK(constructed == 7);
    CHECK(fixture.Call("CallIntMethod", object.Value(), value) == 42U);
    CHECK(fixture.Call("GetObjectClass", object.Value()) != 0U);
    CHECK(fixture.Call(
              "IsInstanceOf", object.Value(), class_reference.Value()) == 1U);
    CHECK(fixture.classes.FindClass("fixture/Counter") == java_class);
}

TEST_CASE("guest JNI static field family uses exact descriptors and A32 ABI") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    static_cast<void>(fixture.classes.RegisterClass(
        {"fixture/Statics", {}, {},
         {{"count", "I", "statics.count", true},
          {"wide", "J", "statics.wide", true},
          {"ratio", "D", "statics.ratio", true},
          {"object", "Ljava/lang/Object;", "statics.object", true}}}));
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    fixture.WriteString(0x100U, "fixture/Statics");
    fixture.WriteString(0x140U, "count");
    fixture.WriteString(0x160U, "I");
    fixture.WriteString(0x180U, "wide");
    fixture.WriteString(0x1a0U, "J");
    fixture.WriteString(0x1c0U, "ratio");
    fixture.WriteString(0x1e0U, "D");
    fixture.WriteString(0x200U, "object");
    fixture.WriteString(0x220U, "Ljava/lang/Object;");
    CHECK(fixture.dispatcher.IsEnvironmentBound(
        *FindJniSlot("GetStaticFieldID")));
    constexpr std::string_view field_types[]{
        "Object", "Boolean", "Byte", "Char", "Short",
        "Int", "Long", "Float", "Double"};
    for (const auto type : field_types) {
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *FindJniSlot(std::string("GetStatic") + std::string(type) +
                         "Field")));
        CHECK(fixture.dispatcher.IsEnvironmentBound(
            *FindJniSlot(std::string("SetStatic") + std::string(type) +
                         "Field")));
    }
    fixture.dispatcher.Seal();

    const auto java_class = fixture.Call(
        "FindClass", fixture.output.Add(0x100U).Value());
    const auto count = fixture.Call(
        "GetStaticFieldID", java_class, fixture.output.Add(0x140U).Value(),
        fixture.output.Add(0x160U).Value());
    const auto wide = fixture.Call(
        "GetStaticFieldID", java_class, fixture.output.Add(0x180U).Value(),
        fixture.output.Add(0x1a0U).Value());
    const auto ratio = fixture.Call(
        "GetStaticFieldID", java_class, fixture.output.Add(0x1c0U).Value(),
        fixture.output.Add(0x1e0U).Value());
    const auto object = fixture.Call(
        "GetStaticFieldID", java_class, fixture.output.Add(0x200U).Value(),
        fixture.output.Add(0x220U).Value());

    static_cast<void>(fixture.Call(
        "SetStaticIntField", java_class, count, 0xfffffff9U));
    CHECK(fixture.Call("GetStaticIntField", java_class, count) ==
          0xfffffff9U);

    constexpr JniLong wide_value = INT64_C(-0x102030405060708);
    fixture.Write64(std::bit_cast<std::uint64_t>(wide_value));
    static_cast<void>(fixture.Call(
        "SetStaticLongField", java_class, wide));
    const auto wide_result = fixture.CallPair(
        "GetStaticLongField", java_class, wide);
    CHECK((static_cast<std::uint64_t>(wide_result[1]) << 32U |
           wide_result[0]) == std::bit_cast<std::uint64_t>(wide_value));

    constexpr JniDouble ratio_value = -19.25;
    fixture.Write64(std::bit_cast<std::uint64_t>(ratio_value));
    static_cast<void>(fixture.Call(
        "SetStaticDoubleField", java_class, ratio));
    const auto ratio_result = fixture.CallPair(
        "GetStaticDoubleField", java_class, ratio);
    CHECK((static_cast<std::uint64_t>(ratio_result[1]) << 32U |
           ratio_result[0]) == std::bit_cast<std::uint64_t>(ratio_value));

    const auto object_identity = AllocateJniHostObjectIdentity();
    const auto object_reference = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, object_identity);
    static_cast<void>(fixture.Call(
        "SetStaticObjectField", java_class, object,
        object_reference.Value()));
    CHECK(fixture.Call("GetStaticObjectField", java_class, object) ==
          object_reference.Value());

    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "GetStaticFloatField", java_class, count)),
        "GetStaticFloatField type does not match field descriptor",
        JniGuestBindingError);
}

TEST_CASE("guest JNI instance calls accept framework registered host objects") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto service_class = fixture.classes.RegisterClass(
        {"fixture/Service", {},
         {{"value", "()I", "service.value", false}}, {}});
    fixture.invocations.RegisterHandler(
        "service.value", [](const JniInvocation&) {
            return JniValue{JniInt{73}};
        });
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto service = AllocateJniHostObjectIdentity();
    fixture.objects.Register(service, service_class);
    const auto reference = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, service);
    fixture.WriteString(0x100U, "fixture/Service");
    fixture.WriteString(0x140U, "value");
    fixture.WriteString(0x160U, "()I");
    fixture.dispatcher.Seal();

    const auto class_reference = fixture.Call(
        "FindClass", fixture.output.Add(0x100U).Value());
    const auto method = fixture.Call(
        "GetMethodID", class_reference, fixture.output.Add(0x140U).Value(),
        fixture.output.Add(0x160U).Value());
    CHECK(fixture.Call("CallIntMethod", reference.Value(), method) == 73U);
    CHECK(fixture.Call("GetObjectClass", reference.Value()) != 0U);
    CHECK(fixture.Call(
              "IsInstanceOf", reference.Value(), class_reference) == 1U);
    CHECK_THROWS_WITH_AS(
        fixture.objects.Register(service, service_class),
        "JNI guest object is already registered", JniGuestBindingError);
    CHECK_THROWS_WITH_AS(
        fixture.objects.Register(AllocateJniHostObjectIdentity(), {}),
        "JNI guest object registration is invalid", JniGuestBindingError);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.objects.ClassOf(
            AllocateJniHostObjectIdentity())),
        "JNI guest receiver is not a registered instance",
        JniGuestBindingError);
}
