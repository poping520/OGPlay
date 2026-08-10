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

class InstanceFixture final {
public:
    InstanceFixture()
        : bus(memory), cpu(bus), abi(memory), dispatcher(ledger),
          invocations(classes), java_vm(environment) {
        memory.Map({output, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        ogplay::runtime::BindJniGuestCoreSlots(
            dispatcher, environment, classes, invocations, strings, arrays,
            java_vm, memory);
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
        REQUIRE(dispatcher.Handle(cpu, stopped));
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
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
