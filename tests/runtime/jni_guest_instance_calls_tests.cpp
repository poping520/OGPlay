#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni_guest/jni_guest_bindings.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_calls.h"
#include "ogplay/runtime/jni_guest/jni_guest_static_fields.h"
#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_java_vm.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/jni/jni_object_array.h"

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
        ogplay::runtime::JniGuestBindingContext context{
            environment, classes, invocations, fields, strings, arrays,
            java_vm, objects, memory};
        ogplay::runtime::BindJniGuestSlots(dispatcher, context);
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

    void WriteUtf16(const std::uint32_t offset,
                    const std::span<const ogplay::runtime::JniChar> value) {
        std::vector<std::byte> bytes(value.size() * 2U);
        for (std::size_t index = 0; index < value.size(); ++index) {
            bytes[index * 2U] =
                static_cast<std::byte>(value[index] & 0xffU);
            bytes[index * 2U + 1U] =
                static_cast<std::byte>(value[index] >> 8U);
        }
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

TEST_CASE("guest JNI jclass is a java.lang.Class instance receiver") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto object_class = fixture.classes.RegisterClass(
        {"java/lang/Object", {}, {}, {}});
    const auto class_class = fixture.classes.RegisterClass(
        {"java/lang/Class", "java/lang/Object",
         {{"getClassLoader", "()Ljava/lang/ClassLoader;",
           "class.getClassLoader", false}}, {}});
    const auto loader_class = fixture.classes.RegisterClass(
        {"java/lang/ClassLoader", "java/lang/Object", {}, {}});
    const auto target_class = fixture.classes.RegisterClass(
        {"fixture/NativeCallback", "java/lang/Object", {}, {}});
    static_cast<void>(object_class);

    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto loader = fixture.objects.Allocate(loader_class);
    const auto loader_reference = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, loader);
    fixture.invocations.RegisterHandler(
        "class.getClassLoader",
        [&](const JniInvocation& invocation) {
            CHECK(fixture.environment.ResolveObjectForHle(
                      InstanceFixture::thread_id, invocation.receiver) ==
                  target_class);
            return JniValue{loader_reference};
        });
    fixture.WriteString(0x100U, "fixture/NativeCallback");
    fixture.WriteString(0x140U, "java/lang/Class");
    fixture.WriteString(0x180U, "getClassLoader");
    fixture.WriteString(0x1c0U, "()Ljava/lang/ClassLoader;");
    fixture.dispatcher.Seal();

    const auto target_reference = JniReference{fixture.Call(
        "FindClass", fixture.output.Add(0x100U).Value())};
    const auto class_reference = JniReference{fixture.Call(
        "FindClass", fixture.output.Add(0x140U).Value())};
    REQUIRE_FALSE(target_reference.IsNull());
    REQUIRE_FALSE(class_reference.IsNull());
    const auto method = fixture.Call(
        "GetMethodID", class_reference.Value(),
        fixture.output.Add(0x180U).Value(),
        fixture.output.Add(0x1c0U).Value());
    REQUIRE(method != 0U);

    const auto result = JniReference{fixture.Call(
        "CallObjectMethod", target_reference.Value(), method)};
    CHECK(fixture.environment.ResolveObjectForHle(
              InstanceFixture::thread_id, result) == loader);
    const auto runtime_class = JniReference{fixture.Call(
        "GetObjectClass", target_reference.Value())};
    CHECK(fixture.environment.ResolveObjectForHle(
              InstanceFixture::thread_id, runtime_class) == class_class);
    CHECK(fixture.Call("IsInstanceOf", target_reference.Value(),
                       class_reference.Value()) == 1U);
}

TEST_CASE("guest JNI primitive arrays preserve region and lease semantics") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    fixture.dispatcher.Seal();

    const auto array = JniReference{fixture.Call("NewIntArray", 3U)};
    REQUIRE_FALSE(array.IsNull());
    CHECK(fixture.Call("GetArrayLength", array.Value()) == 3U);

    fixture.bus.Write32(fixture.output.Add(0x200U), 10U);
    fixture.bus.Write32(fixture.output.Add(0x204U), 20U);
    fixture.bus.Write32(fixture.output.Add(0x208U), 30U);
    fixture.Write64(fixture.output.Add(0x200U).Value());
    static_cast<void>(
        fixture.Call("SetIntArrayRegion", array.Value(), 0U, 3U));

    fixture.Write64(fixture.output.Add(0x300U).Value());
    static_cast<void>(
        fixture.Call("GetIntArrayRegion", array.Value(), 0U, 3U));
    CHECK(fixture.bus.Read32(fixture.output.Add(0x300U)) == 10U);
    CHECK(fixture.bus.Read32(fixture.output.Add(0x304U)) == 20U);
    CHECK(fixture.bus.Read32(fixture.output.Add(0x308U)) == 30U);

    const auto elements = fixture.Call(
        "GetIntArrayElements", array.Value(),
        fixture.output.Add(0x20U).Value());
    REQUIRE(elements != 0U);
    CHECK(fixture.bus.Read8(fixture.output.Add(0x20U)) == 1U);
    fixture.bus.Write32(ogplay::memory::GuestAddress{elements}.Add(4U), 99U);
    static_cast<void>(fixture.Call(
        "ReleaseIntArrayElements", array.Value(), elements, 1U));
    fixture.bus.Write32(ogplay::memory::GuestAddress{elements}.Add(8U), 77U);
    static_cast<void>(fixture.Call(
        "ReleaseIntArrayElements", array.Value(), elements, 2U));

    fixture.Write64(fixture.output.Add(0x300U).Value());
    static_cast<void>(
        fixture.Call("GetIntArrayRegion", array.Value(), 0U, 3U));
    CHECK(fixture.bus.Read32(fixture.output.Add(0x304U)) == 99U);
    CHECK(fixture.bus.Read32(fixture.output.Add(0x308U)) == 30U);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "ReleaseIntArrayElements", array.Value(), elements, 0U)),
        "ReleaseIntArrayElements pointer does not match an active lease",
        JniGuestBindingError);

    const auto second_lease = fixture.Call(
        "GetIntArrayElements", array.Value(), 0U);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "ReleaseIntArrayElements", array.Value(), second_lease, 3U)),
        "ReleaseIntArrayElements mode must be 0, JNI_COMMIT or JNI_ABORT",
        JniGuestBindingError);
    static_cast<void>(fixture.Call(
        "ReleaseIntArrayElements", array.Value(), second_lease, 2U));

    const auto critical = fixture.Call(
        "GetPrimitiveArrayCritical", array.Value(),
        fixture.output.Add(0x20U).Value());
    REQUIRE(critical != 0U);
    CHECK(fixture.bus.Read8(fixture.output.Add(0x20U)) == 1U);
    fixture.bus.Write32(ogplay::memory::GuestAddress{critical}, 44U);
    static_cast<void>(fixture.Call(
        "ReleasePrimitiveArrayCritical", array.Value(), critical, 0U));
    fixture.Write64(fixture.output.Add(0x300U).Value());
    static_cast<void>(
        fixture.Call("GetIntArrayRegion", array.Value(), 0U, 3U));
    CHECK(fixture.bus.Read32(fixture.output.Add(0x300U)) == 44U);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "ReleasePrimitiveArrayCritical", array.Value(), critical, 0U)),
        "ReleasePrimitiveArrayCritical pointer does not match an active lease",
        JniGuestBindingError);
}

TEST_CASE("guest JNI object arrays enforce assignability and null semantics") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto base = fixture.classes.RegisterClass({"fixture/Base", {}, {}, {}});
    const auto child = fixture.classes.RegisterClass(
        {"fixture/Child", "fixture/Base", {}, {}});
    const auto other = fixture.classes.RegisterClass(
        {"fixture/Other", {}, {}, {}});
    const auto child_object = fixture.objects.Allocate(child);
    const auto other_object = fixture.objects.Allocate(other);
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto base_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, base);
    const auto child_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, child_object);
    const auto other_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, other_object);
    fixture.dispatcher.Seal();

    const auto array = JniReference{fixture.Call(
        "NewObjectArray", 2U, base_ref.Value(), child_ref.Value())};
    REQUIRE_FALSE(array.IsNull());
    CHECK(fixture.Call("GetArrayLength", array.Value()) == 2U);
    const auto element = JniReference{fixture.Call(
        "GetObjectArrayElement", array.Value(), 0U)};
    CHECK(fixture.environment.IsSameObject(
        InstanceFixture::thread_id, element, child_ref));

    static_cast<void>(fixture.Call(
        "SetObjectArrayElement", array.Value(), 1U, 0U));
    CHECK(fixture.Call("GetObjectArrayElement", array.Value(), 1U) == 0U);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "SetObjectArrayElement", array.Value(), 0U,
            other_ref.Value())),
        JniObjectArrayError);
}

TEST_CASE("guest JNI nonvirtual calls decode stacked normal V and A arguments") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto base = fixture.classes.RegisterClass(
        {"fixture/NonvirtualBase", {},
         {{"value", "(I)I", "nonvirtual.base", false}}, {}});
    const auto child = fixture.classes.RegisterClass(
        {"fixture/NonvirtualChild", "fixture/NonvirtualBase",
         {{"value", "(I)I", "nonvirtual.child", false}}, {}});
    const auto method = fixture.classes.GetMethodId(
        base, "value", "(I)I", false);
    REQUIRE(method.has_value());
    std::vector<JniArgumentSource> sources;
    fixture.invocations.RegisterHandler(
        "nonvirtual.base",
        [&sources, base](const JniInvocation& invocation) {
            CHECK(invocation.kind == JniInvocationKind::nonvirtual_instance);
            CHECK(invocation.dispatch_class == base);
            sources.push_back(invocation.argument_source);
            return JniValue{std::get<JniInt>(invocation.arguments[0]) + 1};
        });
    fixture.invocations.RegisterHandler(
        "nonvirtual.child", [](const JniInvocation&) {
            return JniValue{JniInt{-1}};
        });
    const auto object = fixture.objects.Allocate(child);
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto object_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, object);
    const auto class_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, base);
    fixture.dispatcher.Seal();

    fixture.Write64(41U);
    CHECK(fixture.Call("CallNonvirtualIntMethod", object_ref.Value(),
                       class_ref.Value(), method->Value()) == 42U);
    fixture.bus.Write32(fixture.output.Add(0x300U), 41U);
    fixture.Write64(fixture.output.Add(0x300U).Value());
    CHECK(fixture.Call("CallNonvirtualIntMethodV", object_ref.Value(),
                       class_ref.Value(), method->Value()) == 42U);
    fixture.bus.Write32(fixture.output.Add(0x340U), 41U);
    fixture.Write64(fixture.output.Add(0x340U).Value());
    CHECK(fixture.Call("CallNonvirtualIntMethodA", object_ref.Value(),
                       class_ref.Value(), method->Value()) == 42U);
    CHECK(sources == std::vector{JniArgumentSource::variadic,
                                 JniArgumentSource::va_list,
                                 JniArgumentSource::value_array});
}

TEST_CASE("guest JNI ThrowNew describes one stable pending throwable") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto exception_class = fixture.classes.RegisterClass(
        {"fixture/Failure", {}, {}, {}});
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto class_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, exception_class);
    fixture.WriteString(0x200U, "broken input");
    fixture.dispatcher.Seal();

    CHECK(fixture.Call("ThrowNew", class_ref.Value(),
                       fixture.output.Add(0x200U).Value()) == 0U);
    CHECK(fixture.Call("ExceptionCheck") == 1U);
    const auto first = JniReference{fixture.Call("ExceptionOccurred")};
    REQUIRE_FALSE(first.IsNull());
    static_cast<void>(fixture.Call("ExceptionDescribe"));
    CHECK(fixture.Call("ExceptionCheck") == 1U);
    const auto second = JniReference{fixture.Call("ExceptionOccurred")};
    const auto diagnostics = fixture.environment.ExceptionDiagnostics();
    REQUIRE(diagnostics.size() == 1U);
    CHECK(diagnostics[0].guest_thread == InstanceFixture::thread_id);
    CHECK(std::get<std::uint64_t>(diagnostics[0].fields[1].value) ==
          exception_class.value);
    CHECK(std::get<std::string>(diagnostics[0].fields[2].value) ==
          "broken input");
    static_cast<void>(fixture.Call("ExceptionClear"));
    CHECK(fixture.environment.IsSameObject(
        InstanceFixture::thread_id, first, second));
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

TEST_CASE("guest JNI instance fields resolve receivers and preserve A32 values") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    static_cast<void>(fixture.classes.RegisterClass(
        {"java/lang/Object", {}, {}, {}}));
    const auto holder_class = fixture.classes.RegisterClass(
        {"fixture/Holder", "java/lang/Object", {},
         {{"count", "I", "holder.count", false},
          {"wide", "J", "holder.wide", false},
          {"ratio", "D", "holder.ratio", false},
          {"token", "Ljava/lang/Object;", "holder.token", false},
          {"shared", "I", "holder.shared", true}}});
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto holder = fixture.objects.Allocate(holder_class);
    const auto holder_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, holder);
    const auto token = fixture.objects.Allocate(
        *fixture.classes.FindClass("java/lang/Object"));
    const auto token_ref = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, token);
    fixture.WriteString(0x100U, "fixture/Holder");
    fixture.WriteString(0x140U, "count");
    fixture.WriteString(0x160U, "I");
    fixture.WriteString(0x180U, "wide");
    fixture.WriteString(0x1a0U, "J");
    fixture.WriteString(0x1c0U, "ratio");
    fixture.WriteString(0x1e0U, "D");
    fixture.WriteString(0x200U, "token");
    fixture.WriteString(0x220U, "Ljava/lang/Object;");
    fixture.WriteString(0x240U, "shared");
    fixture.dispatcher.Seal();

    const auto java_class = fixture.Call(
        "FindClass", fixture.output.Add(0x100U).Value());
    const auto count = fixture.Call(
        "GetFieldID", java_class, fixture.output.Add(0x140U).Value(),
        fixture.output.Add(0x160U).Value());
    const auto wide = fixture.Call(
        "GetFieldID", java_class, fixture.output.Add(0x180U).Value(),
        fixture.output.Add(0x1a0U).Value());
    const auto ratio = fixture.Call(
        "GetFieldID", java_class, fixture.output.Add(0x1c0U).Value(),
        fixture.output.Add(0x1e0U).Value());
    const auto token_field = fixture.Call(
        "GetFieldID", java_class, fixture.output.Add(0x200U).Value(),
        fixture.output.Add(0x220U).Value());
    const auto shared = fixture.Call(
        "GetStaticFieldID", java_class,
        fixture.output.Add(0x240U).Value(),
        fixture.output.Add(0x160U).Value());

    static_cast<void>(fixture.Call(
        "SetIntField", holder_ref.Value(), count, 0xfffffff9U));
    CHECK(fixture.Call("GetIntField", holder_ref.Value(), count) ==
          0xfffffff9U);
    constexpr JniLong wide_value = INT64_C(-0x11223344556677);
    fixture.Write64(std::bit_cast<std::uint64_t>(wide_value));
    static_cast<void>(fixture.Call(
        "SetLongField", holder_ref.Value(), wide));
    const auto wide_result = fixture.CallPair(
        "GetLongField", holder_ref.Value(), wide);
    CHECK((static_cast<std::uint64_t>(wide_result[1]) << 32U |
           wide_result[0]) == std::bit_cast<std::uint64_t>(wide_value));
    constexpr JniDouble ratio_value = 41.5;
    fixture.Write64(std::bit_cast<std::uint64_t>(ratio_value));
    static_cast<void>(fixture.Call(
        "SetDoubleField", holder_ref.Value(), ratio));
    const auto ratio_result = fixture.CallPair(
        "GetDoubleField", holder_ref.Value(), ratio);
    CHECK((static_cast<std::uint64_t>(ratio_result[1]) << 32U |
           ratio_result[0]) == std::bit_cast<std::uint64_t>(ratio_value));
    static_cast<void>(fixture.Call(
        "SetObjectField", holder_ref.Value(), token_field,
        token_ref.Value()));
    CHECK(fixture.Call(
              "GetObjectField", holder_ref.Value(), token_field) ==
          token_ref.Value());

    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "GetFloatField", holder_ref.Value(), count)),
        JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "GetIntField", holder_ref.Value(), shared)),
        JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "GetStaticIntField", java_class, count)),
        JniFieldStoreError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call("GetIntField", 0x1234U, count)),
        std::exception);
}

TEST_CASE("guest JNI UTF-16 strings own checked code-unit leases") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const std::array<JniChar, 4> text{'A', 0xd83dU, 0xde00U, 0U};
    fixture.WriteUtf16(0x100U, text);
    fixture.dispatcher.Seal();

    const auto string = JniReference{fixture.Call(
        "NewString", fixture.output.Add(0x100U).Value(),
        static_cast<std::uint32_t>(text.size()))};
    REQUIRE_FALSE(string.IsNull());
    CHECK(fixture.Call("GetStringLength", string.Value()) == 4U);
    const auto pointer = fixture.Call(
        "GetStringChars", string.Value(), fixture.output.Add(0x300U).Value());
    REQUIRE(pointer != 0U);
    CHECK(fixture.bus.Read8(fixture.output.Add(0x300U)) == 1U);
    for (std::size_t index = 0; index < text.size(); ++index) {
        CHECK(fixture.bus.Read16(
                  ogplay::memory::GuestAddress{pointer}.Add(index * 2U)) ==
              text[index]);
    }
    fixture.bus.Write32(fixture.output.Add(0x800U),
                        fixture.output.Add(0x400U).Value());
    static_cast<void>(fixture.Call(
        "GetStringRegion", string.Value(), 1U, 2U));
    CHECK(fixture.bus.Read16(fixture.output.Add(0x400U)) == 0xd83dU);
    CHECK(fixture.bus.Read16(fixture.output.Add(0x402U)) == 0xde00U);

    const auto other = JniReference{fixture.Call(
        "NewString", fixture.output.Add(0x100U).Value(),
        static_cast<std::uint32_t>(text.size()))};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "ReleaseStringChars", other.Value(), pointer)),
        "ReleaseStringChars pointer does not match an active lease",
        JniGuestBindingError);
    static_cast<void>(fixture.Call(
        "ReleaseStringChars", string.Value(), pointer));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "ReleaseStringChars", string.Value(), pointer)),
        "ReleaseStringChars pointer does not match an active lease",
        JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "NewString", 0xfffffff0U, 16U)),
        std::exception);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "GetStringRegion", string.Value(), 3U, 2U)),
        JniStringError);
}

namespace {

template <typename Snapshot>
[[nodiscard]] bool WaitForWaiters(const Snapshot snapshot,
                                  const std::size_t expected) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (snapshot().waiting_threads != expected) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

template <typename Enter>
[[nodiscard]] std::future<std::optional<ogplay::runtime::JniMonitorErrorReason>>
BlockingEnter(const Enter enter) {
    return std::async(
        std::launch::async,
        [enter]() -> std::optional<ogplay::runtime::JniMonitorErrorReason> {
            try {
                enter();
            } catch (const ogplay::runtime::JniMonitorError& error) {
                return error.Reason();
            }
            return std::nullopt;
        });
}

}  // namespace

TEST_CASE("JNI monitor is reentrant and rejects a non-owner exit") {
    using namespace ogplay::runtime;
    JniMonitorTable monitors;
    const auto object = AllocateJniHostObjectIdentity();

    monitors.Enter(object, 1U);
    monitors.Enter(object, 1U);
    CHECK(monitors.Snapshot(object).owner_thread == 1U);
    CHECK(monitors.Snapshot(object).recursion == 2U);
    monitors.Exit(object, 1U);
    CHECK(monitors.Snapshot(object).recursion == 1U);
    CHECK_THROWS_WITH_AS(
        monitors.Exit(object, 2U),
        "JNI monitor exit requires the owning guest thread",
        JniMonitorError);
    monitors.Exit(object, 1U);
    CHECK(monitors.Snapshot(object).owner_thread == 0U);
}

TEST_CASE("JNI monitor contention transfers ownership after detach release") {
    using namespace ogplay::runtime;
    using namespace std::chrono_literals;
    JniEnvironment environment;
    const auto object = AllocateJniHostObjectIdentity();
    environment.AttachThread(11U);
    environment.AttachThread(12U);
    const auto first = environment.PublishLocalObject(11U, object);
    const auto second = environment.PublishLocalObject(12U, object);
    environment.MonitorEnter(11U, first);
    std::atomic<bool> acquired{};
    auto waiter = std::async(std::launch::async, [&] {
        environment.MonitorEnter(12U, second);
        acquired = true;
        environment.MonitorExit(12U, second);
    });
    const auto wait_deadline = std::chrono::steady_clock::now() + 2s;
    while (environment.MonitorSnapshot(object).waiting_threads == 0U &&
           std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::yield();
    }
    const auto blocked =
        environment.MonitorSnapshot(object).waiting_threads == 1U;
    if (!blocked) environment.DetachThread(11U);
    REQUIRE(blocked);
    CHECK_FALSE(acquired.load());
    environment.DetachThread(11U);
    const auto transferred =
        waiter.wait_for(2s) == std::future_status::ready;
    if (!transferred) {
        static_cast<void>(environment.InterruptMonitorWaiters());
    }
    REQUIRE(transferred);
    waiter.get();
    CHECK(acquired.load());
    CHECK(environment.MonitorSnapshot(object).owner_thread == 0U);
    environment.DetachThread(12U);
}

TEST_CASE("JNI monitor waiter interruption keeps the subsystem usable") {
    using namespace ogplay::runtime;
    using namespace std::chrono_literals;
    JniMonitorTable monitors;
    const auto object = AllocateJniHostObjectIdentity();
    monitors.Enter(object, 21U);
    auto waiter = BlockingEnter([&] { monitors.Enter(object, 22U); });
    REQUIRE(WaitForWaiters([&] { return monitors.Snapshot(object); }, 1U));
    CHECK(monitors.InterruptWaiters() == 1U);
    REQUIRE(waiter.wait_for(2s) == std::future_status::ready);
    const auto reason = waiter.get();
    REQUIRE(reason.has_value());
    CHECK(*reason == JniMonitorErrorReason::interrupted);
    CHECK(monitors.Snapshot(object).owner_thread == 21U);
    CHECK_FALSE(monitors.Snapshot(object).shut_down);

    monitors.Exit(object, 21U);
    monitors.Enter(object, 23U);
    CHECK(monitors.Snapshot(object).owner_thread == 23U);
    auto later = BlockingEnter([&] { monitors.Enter(object, 24U); });
    REQUIRE(WaitForWaiters([&] { return monitors.Snapshot(object); }, 1U));
    monitors.Exit(object, 23U);
    REQUIRE(later.wait_for(2s) == std::future_status::ready);
    CHECK_FALSE(later.get().has_value());
    CHECK(monitors.Snapshot(object).owner_thread == 24U);
    monitors.Exit(object, 24U);
}

TEST_CASE("JNI monitor shutdown wakes waiters and rejects later entry") {
    using namespace ogplay::runtime;
    using namespace std::chrono_literals;
    JniMonitorTable monitors;
    const auto object = AllocateJniHostObjectIdentity();
    monitors.Enter(object, 31U);
    auto waiter = BlockingEnter([&] { monitors.Enter(object, 32U); });
    REQUIRE(WaitForWaiters([&] { return monitors.Snapshot(object); }, 1U));
    CHECK(monitors.Shutdown() == 1U);
    REQUIRE(waiter.wait_for(2s) == std::future_status::ready);
    const auto reason = waiter.get();
    REQUIRE(reason.has_value());
    CHECK(*reason == JniMonitorErrorReason::shut_down);
    CHECK(monitors.Snapshot(object).shut_down);
    CHECK(monitors.Shutdown() == 0U);
    monitors.Exit(object, 31U);
    CHECK_THROWS_WITH_AS(monitors.Enter(object, 33U),
                         "JNI monitor table is shut down", JniMonitorError);
}

TEST_CASE("JNI monitor teardown order keeps finalizer locking available") {
    using namespace ogplay::runtime;
    using namespace std::chrono_literals;
    JniEnvironment environment;
    const auto object = AllocateJniHostObjectIdentity();
    constexpr std::uint64_t root = 41U;
    constexpr std::uint64_t child = 42U;
    environment.AttachThread(root);
    environment.AttachThread(child);
    const auto root_reference = environment.PublishLocalObject(root, object);
    const auto child_reference = environment.PublishLocalObject(child, object);
    environment.MonitorEnter(root, root_reference);
    auto guest_child = BlockingEnter(
        [&] { environment.MonitorEnter(child, child_reference); });
    REQUIRE(WaitForWaiters(
        [&] { return environment.MonitorSnapshot(object); }, 1U));

    // Session Stop interrupts temporary waits before joining guest children.
    CHECK(environment.InterruptMonitorWaiters() == 1U);
    REQUIRE(guest_child.wait_for(2s) == std::future_status::ready);
    const auto reason = guest_child.get();
    REQUIRE(reason.has_value());
    CHECK(*reason == JniMonitorErrorReason::interrupted);
    environment.DetachThread(child);

    // Guest finalizers run after the join and may still lock monitors.
    environment.MonitorExit(root, root_reference);
    environment.MonitorEnter(root, root_reference);
    CHECK(environment.MonitorSnapshot(object).owner_thread == root);
    environment.MonitorExit(root, root_reference);

    environment.DetachThread(root);
    CHECK(environment.ShutdownMonitors() == 0U);
    environment.AttachThread(root);
    const auto reopened = environment.PublishLocalObject(root, object);
    CHECK_THROWS_WITH_AS(environment.MonitorEnter(root, reopened),
                         "JNI monitor table is shut down", JniMonitorError);
}

TEST_CASE("guest JNI monitor bindings expose real recursion state") {
    using namespace ogplay::runtime;
    InstanceFixture fixture;
    const auto java_class = fixture.classes.RegisterClass(
        {"fixture/Lock", {}, {}, {}});
    const auto object = fixture.objects.Allocate(java_class);
    fixture.environment.AttachThread(InstanceFixture::thread_id);
    const auto reference = fixture.environment.PublishLocalObject(
        InstanceFixture::thread_id, object);
    fixture.dispatcher.Seal();

    CHECK(fixture.Call("MonitorEnter", reference.Value()) == 0U);
    CHECK(fixture.Call("MonitorEnter", reference.Value()) == 0U);
    CHECK(fixture.environment.MonitorSnapshot(object).recursion == 2U);
    CHECK(fixture.Call("MonitorExit", reference.Value()) == 0U);
    CHECK(fixture.environment.MonitorSnapshot(object).recursion == 1U);
    CHECK(fixture.Call("MonitorExit", reference.Value()) == 0U);
    CHECK(fixture.environment.MonitorSnapshot(object).owner_thread == 0U);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call("MonitorExit", reference.Value())),
        JniMonitorError);
}
