#include <array>
#include <bit>
#include <cstdint>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/cpu/interpreter.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
#include "ogplay/runtime/integration/jni_guest_bindings.h"
#include "ogplay/runtime/integration/jni_guest_dispatch.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_native_registry.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace {

class GuestNativeFixture final {
public:
    GuestNativeFixture()
        : bus(memory), cpu(bus), abi(memory), dispatcher(ledger) {
        memory.Map({data, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        memory.Map({code, memory.PageSize()},
                   ogplay::memory::PageProtection::read |
                       ogplay::memory::PageProtection::write);
        bus.Write16(code, 0x202aU);         // movs r0, #42
        bus.Write16(code.Add(2U), 0x4770U); // bx lr
        memory.Protect({code, memory.PageSize()},
                       ogplay::memory::PageProtection::read |
                           ogplay::memory::PageProtection::execute);
        ogplay::runtime::BindJniGuestNativeRegistrationSlots(
            dispatcher, environment, classes, natives, memory);
        dispatcher.Seal();
        environment.AttachThread(thread_id, 32U);
    }

    void WriteString(const std::uint32_t offset,
                     const std::string_view value) {
        std::vector<std::byte> bytes;
        bytes.reserve(value.size() + 1U);
        for (const auto character : value) {
            bytes.push_back(static_cast<std::byte>(
                static_cast<unsigned char>(character)));
        }
        bytes.push_back(std::byte{});
        memory.Write(data.Add(offset), bytes, thread_id);
    }

    void WriteMethod(const std::uint32_t offset,
                     const std::uint32_t name_offset,
                     const std::uint32_t descriptor_offset,
                     const std::uint32_t target) {
        bus.Write32(data.Add(offset), data.Add(name_offset).Value(), thread_id);
        bus.Write32(data.Add(offset + 4U),
                    data.Add(descriptor_offset).Value(), thread_id);
        bus.Write32(data.Add(offset + 8U), target, thread_id);
    }

    [[nodiscard]] std::uint32_t Call(
        const std::string_view name, const std::uint32_t r1,
        const std::uint32_t r2 = 0U, const std::uint32_t r3 = 0U) {
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
                          data.Add(0x800U).Value());
        state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x12345679U);
        cpu.SetState(state);
        const auto stopped = cpu.Run(1U);
        if (!dispatcher.Handle(cpu, stopped)) {
            throw std::runtime_error("guest native binding trap was rejected");
        }
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }

    [[nodiscard]] std::uint32_t Execute(
        const ogplay::runtime::A32GuestCallFrame& frame) {
        ogplay::cpu::A32State state;
        state.SetState((frame.target.Value() & 1U) != 0U
                           ? ogplay::cpu::ExecutionState::thumb
                           : ogplay::cpu::ExecutionState::a32);
        state.SetThreadId(thread_id);
        state.SetRegister(ogplay::cpu::CoreRegister::pc,
                          frame.target.Value() & ~1U);
        for (std::size_t index = 0; index < frame.registers.size(); ++index) {
            state.SetRegister(static_cast<ogplay::cpu::CoreRegister>(index),
                              frame.registers[index]);
        }
        state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x12345679U);
        cpu.SetState(state);
        static_cast<void>(cpu.Run(2U));
        return cpu.GetState().Register(ogplay::cpu::CoreRegister::r0);
    }

    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus;
    ogplay::cpu::InterpreterCpu cpu;
    ogplay::runtime::GuestJniAbi abi;
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniGuestCallDispatcher dispatcher;
    ogplay::runtime::JniEnvironment environment;
    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniNativeRegistry natives;
    const ogplay::memory::GuestAddress data{0x72000000U};
    const ogplay::memory::GuestAddress code{0x73000000U};
    const std::uint64_t thread_id{501U};
};

}  // namespace

TEST_CASE("RegisterNatives resolves overloads by class name and descriptor") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto java_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const std::array methods{
        ogplay::runtime::JniNativeMethod{
            "step", "(I)I", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "step", "(J)J", ogplay::memory::GuestAddress{0x71002000}},
    };
    registry.RegisterNatives(java_class, methods);
    CHECK(registry.Resolve(java_class, "step", "(I)I") ==
          ogplay::memory::GuestAddress{0x71001000});
    CHECK(registry.Resolve(java_class, "step", "(J)J") ==
          ogplay::memory::GuestAddress{0x71002000});
    CHECK_FALSE(registry.Resolve(java_class, "step", "()V").has_value());
    const auto declarations = registry.Declarations(java_class);
    REQUIRE(declarations.size() == 2);
    CHECK(declarations[0].descriptor == "(I)I");
    CHECK(declarations[1].descriptor == "(J)J");

    // Identical registration is deterministic and idempotent.
    registry.RegisterNatives(java_class, methods);
    CHECK(registry.Declarations(java_class).size() == 2);
}

TEST_CASE("RegisterNatives validates a whole batch before mutation") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto java_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const std::array malformed{
        ogplay::runtime::JniNativeMethod{
            "valid", "()V", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "broken", "(V)V", ogplay::memory::GuestAddress{0x71002000}},
    };
    CHECK_THROWS_AS(registry.RegisterNatives(java_class, malformed),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Declarations(java_class).empty());

    const std::array duplicate{
        ogplay::runtime::JniNativeMethod{
            "same", "()V", ogplay::memory::GuestAddress{0x71001000}},
        ogplay::runtime::JniNativeMethod{
            "same", "()V", ogplay::memory::GuestAddress{0x71001000}},
    };
    CHECK_THROWS_AS(registry.RegisterNatives(java_class, duplicate),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Declarations(java_class).empty());
}

TEST_CASE("RegisterNatives rejects target conflicts and unregisters one class") {
    ogplay::runtime::JniNativeRegistry registry;
    const auto first_class = ogplay::runtime::AllocateJniHostObjectIdentity();
    const ogplay::runtime::JniObjectIdentity second_class{
        ogplay::runtime::JniObjectDomain::dex_vm, 1};
    const std::array first{ogplay::runtime::JniNativeMethod{
        "run", "()V", ogplay::memory::GuestAddress{0x71001000}}};
    const std::array conflict{ogplay::runtime::JniNativeMethod{
        "run", "()V", ogplay::memory::GuestAddress{0x71002000}}};
    registry.RegisterNatives(first_class, first);
    registry.RegisterNatives(second_class, conflict);
    CHECK_THROWS_AS(registry.RegisterNatives(first_class, conflict),
                    ogplay::runtime::JniNativeRegistryError);
    CHECK(registry.Resolve(first_class, "run", "()V") == first[0].target);
    CHECK(registry.UnregisterNatives(first_class) == 1);
    CHECK_FALSE(registry.Resolve(first_class, "run", "()V").has_value());
    CHECK(registry.Resolve(second_class, "run", "()V") ==
          conflict[0].target);
    CHECK(registry.UnregisterNatives(first_class) == 0);
}

TEST_CASE("guest RegisterNatives drives a real Thumb native and unregisters by class") {
    using namespace ogplay;
    GuestNativeFixture fixture;
    const auto first_class = fixture.classes.RegisterClass(
        {"example/First", std::nullopt,
         {{"nativeStep", "(I)I", "native.first.step", false}}, {}});
    const auto second_class = fixture.classes.RegisterClass(
        {"example/Second", std::nullopt,
         {{"nativeStep", "(I)I", "native.second.step", true},
          {"nativeOther", "()V", "native.second.other", true}},
         {}});
    const auto first_ref = fixture.environment.PublishLocalObject(
        fixture.thread_id, first_class);
    const auto second_ref = fixture.environment.PublishLocalObject(
        fixture.thread_id, second_class);

    fixture.WriteString(0x100U, "nativeStep");
    fixture.WriteString(0x120U, "(I)I");
    fixture.WriteMethod(0x200U, 0x100U, 0x120U,
                        fixture.code.Value() | 1U);
    CHECK(fixture.Call("RegisterNatives", first_ref.Value(),
                       fixture.data.Add(0x200U).Value(), 1U) == 0U);
    CHECK(fixture.natives.Resolve(first_class, "nativeStep", "(I)I") ==
          memory::GuestAddress{fixture.code.Value() | 1U});

    runtime::A32GuestCallFrame call;
    call.registers = {fixture.abi.Environment().Value(), first_ref.Value(),
                      40U, 0U};
    const auto resolved = runtime::ResolveJniRegisteredNativeCall(
        fixture.natives, first_class, "nativeStep", "(I)I", call);
    CHECK(resolved.target ==
          memory::GuestAddress{fixture.code.Value() | 1U});
    CHECK(fixture.Execute(resolved) == 42U);

    fixture.WriteString(0x140U, "nativeOther");
    fixture.WriteString(0x160U, "()V");
    fixture.WriteMethod(0x20cU, 0x140U, 0x160U,
                        fixture.code.Value() | 1U);
    CHECK(fixture.Call("RegisterNatives", second_ref.Value(),
                       fixture.data.Add(0x20cU).Value(), 1U) == 0U);

    CHECK(fixture.Call("UnregisterNatives", first_ref.Value()) == 0U);
    CHECK_FALSE(
        fixture.natives.Resolve(first_class, "nativeStep", "(I)I")
            .has_value());
    CHECK(fixture.natives.Resolve(second_class, "nativeOther", "()V") ==
          memory::GuestAddress{fixture.code.Value() | 1U});
    CHECK_THROWS_WITH_AS(
        static_cast<void>(runtime::ResolveJniRegisteredNativeCall(
            fixture.natives, first_class, "nativeStep", "(I)I", call)),
        "registered JNI native method is unresolved: nativeStep(I)I",
        runtime::JniGuestBindingError);
}

TEST_CASE("guest RegisterNatives validates its complete ARM32 batch") {
    using namespace ogplay;
    GuestNativeFixture fixture;
    const auto java_class = fixture.classes.RegisterClass(
        {"example/Batch", std::nullopt,
         {{"first", "()V", "native.batch.first", true},
          {"second", "()V", "native.batch.second", true}},
         {}});
    const auto class_ref = fixture.environment.PublishLocalObject(
        fixture.thread_id, java_class);
    fixture.WriteString(0x100U, "first");
    fixture.WriteString(0x120U, "()V");
    fixture.WriteString(0x140U, "second");
    fixture.WriteString(0x160U, "(V)V");
    fixture.WriteMethod(0x200U, 0x100U, 0x120U,
                        fixture.code.Value() | 1U);
    fixture.WriteMethod(0x20cU, 0x140U, 0x160U,
                        fixture.code.Value() | 1U);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "RegisterNatives", class_ref.Value(),
            fixture.data.Add(0x200U).Value(), 2U)),
        runtime::JniGuestBindingError);
    CHECK(fixture.natives.Declarations(java_class).empty());
    CHECK_THROWS_WITH_AS(
        static_cast<void>(fixture.Call(
            "RegisterNatives", class_ref.Value(),
            fixture.data.Add(0x200U).Value(), 0xffffffffU)),
        "RegisterNatives method count cannot be negative",
        runtime::JniGuestBindingError);
    CHECK_THROWS_AS(
        static_cast<void>(fixture.Call(
            "RegisterNatives", class_ref.Value(), 0xfffffff8U, 1U)),
        std::exception);
    CHECK(fixture.natives.Declarations(java_class).empty());
}
