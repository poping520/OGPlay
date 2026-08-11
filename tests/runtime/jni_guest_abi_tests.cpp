#include <array>
#include <cstddef>
#include <cstdint>

#include <doctest/doctest.h>

#include "ogplay/memory/address_space.h"
#include "ogplay/memory/bus.h"
#include "ogplay/cpu/interpreter.h"
#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni/jni_java_vm.h"

namespace {

[[nodiscard]] std::uint32_t Read32(
    ogplay::memory::AddressSpace& memory,
    const ogplay::memory::GuestAddress address) {
    std::array<std::byte, 4> bytes{};
    memory.Read(address, bytes);
    std::uint32_t result{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<std::uint8_t>(bytes[index]))
                  << static_cast<unsigned>(index * 8U);
    }
    return result;
}

}  // namespace

TEST_CASE("guest JNI ABI publishes complete 32-bit interface objects") {
    ogplay::memory::AddressSpace memory;
    {
        const ogplay::runtime::GuestJniAbi abi(memory);
        CHECK(abi.Environment() == ogplay::runtime::kJniGuestEnvironment);
        CHECK(abi.JavaVm() == ogplay::runtime::kJniGuestJavaVm);
        CHECK(Read32(memory, abi.Environment()) ==
              ogplay::runtime::kJniGuestEnvironmentTable.Value());
        CHECK(Read32(memory, abi.JavaVm()) ==
              ogplay::runtime::kJniGuestInvokeTable.Value());

        for (std::size_t slot = 0;
             slot < ogplay::runtime::kJniReservedSlotCount; ++slot) {
            CHECK(Read32(
                      memory,
                      ogplay::runtime::kJniGuestEnvironmentTable.Add(
                          slot * sizeof(std::uint32_t))) == 0U);
        }
        for (std::size_t slot = 0; slot < 3U; ++slot) {
            CHECK(Read32(
                      memory,
                      ogplay::runtime::kJniGuestInvokeTable.Add(
                          slot * sizeof(std::uint32_t))) == 0U);
        }

        const auto get_version =
            ogplay::runtime::FindJniSlot("GetVersion")->Value();
        const auto direct_buffer =
            ogplay::runtime::FindJniSlot("NewDirectByteBuffer")->Value();
        const auto destroy_vm =
            ogplay::runtime::FindJniInvokeSlot("DestroyJavaVM")->Value();
        const auto get_env =
            ogplay::runtime::FindJniInvokeSlot("GetEnv")->Value();
        for (const auto slot : {get_version, direct_buffer}) {
            const auto target = Read32(
                memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                            slot * sizeof(std::uint32_t)));
            const auto described = ogplay::runtime::DescribeJniGuestThunk(
                ogplay::memory::GuestAddress{target});
            REQUIRE(described.has_value());
            CHECK_FALSE(described->java_vm);
            CHECK(described->slot == slot);
            CHECK((target & 1U) == 1U);
        }
        for (const auto slot : {destroy_vm, get_env}) {
            const auto target = Read32(
                memory, ogplay::runtime::kJniGuestInvokeTable.Add(
                            slot * sizeof(std::uint32_t)));
            const auto described = ogplay::runtime::DescribeJniGuestThunk(
                ogplay::memory::GuestAddress{target});
            REQUIRE(described.has_value());
            CHECK(described->java_vm);
            CHECK(described->slot == slot);
            CHECK((target & 1U) == 1U);
        }

        ogplay::core::CapabilityLedger ledger;
        ogplay::runtime::JniFunctionTable environment_table(ledger);
        ogplay::runtime::JniInvokeFunctionTable invoke_table(ledger);
        const ogplay::runtime::JniCommonSlotDirectory directory;
        directory.Install(environment_table, invoke_table);
        CHECK(environment_table.Resolve(
                  ogplay::runtime::JniSlot{get_version}, 0x1000U)
              .Value() ==
              Read32(memory,
                     ogplay::runtime::kJniGuestEnvironmentTable.Add(
                         get_version * sizeof(std::uint32_t))));
        CHECK(invoke_table.Resolve(
                  ogplay::runtime::JniInvokeSlot{
                      static_cast<std::uint8_t>(get_env)},
                  0x1000U)
              .Value() ==
              Read32(memory,
                     ogplay::runtime::kJniGuestInvokeTable.Add(
                         get_env * sizeof(std::uint32_t))));
        CHECK_THROWS_AS(
            static_cast<void>(environment_table.Resolve(
                ogplay::runtime::JniSlot{direct_buffer}, 0x2000U)),
            ogplay::runtime::JniUnimplementedCall);
        CHECK_THROWS_AS(
            static_cast<void>(invoke_table.Resolve(
                ogplay::runtime::JniInvokeSlot{
                    static_cast<std::uint8_t>(destroy_vm)},
                0x3000U)),
            ogplay::runtime::JniInvokeUnimplementedCall);
    }

    std::array<std::byte, 1> byte{};
    CHECK_THROWS_AS(memory.Read(ogplay::runtime::kJniGuestEnvironment, byte),
                    ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        memory.Fetch(ogplay::memory::GuestAddress{
                         ogplay::runtime::kJniThunkBegin},
                     byte),
        ogplay::memory::MemoryFault);
}

TEST_CASE("guest JNI ABI maps trap code RX and tables read-only") {
    ogplay::memory::AddressSpace memory;
    const ogplay::runtime::GuestJniAbi abi(memory);
    const auto slot = ogplay::runtime::FindJniSlot("GetVersion")->Value();
    const auto target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    slot * sizeof(std::uint32_t)));
    std::array<std::byte, 4> code{};
    memory.Fetch(ogplay::memory::GuestAddress{target & ~1U}, code);
    CHECK(code == std::array<std::byte, 4>{
                      std::byte{0x03}, std::byte{0xdf},
                      std::byte{0x70}, std::byte{0x47}});

    const std::array<std::byte, 1> value{std::byte{0xff}};
    CHECK_THROWS_AS(
        memory.Write(ogplay::runtime::kJniGuestEnvironmentTable, value),
        ogplay::memory::MemoryFault);
    CHECK_THROWS_AS(
        memory.Write(ogplay::memory::GuestAddress{target & ~1U}, value),
        ogplay::memory::MemoryFault);
}

TEST_CASE("guest JNI ABI construction rolls back occupied layouts") {
    ogplay::memory::AddressSpace memory;
    const ogplay::memory::GuestRange occupied{
        ogplay::memory::GuestAddress{ogplay::runtime::kJniInvokeThunkBegin},
        memory.PageSize()};
    memory.Map(occupied, ogplay::memory::PageProtection::read);
    CHECK_THROWS(static_cast<void>(ogplay::runtime::GuestJniAbi(memory)));

    const ogplay::memory::GuestRange environment_page{
        ogplay::memory::GuestAddress{ogplay::runtime::kJniThunkBegin},
        memory.PageSize()};
    CHECK_THROWS_AS(memory.ValidateMapped(environment_page),
                    ogplay::memory::MemoryFault);
    memory.ValidateMapped(occupied);
    memory.Unmap(occupied);
}

TEST_CASE("guest JNI dispatcher decodes an exact environment trap") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::cpu::InterpreterCpu cpu(bus);
    const ogplay::runtime::GuestJniAbi abi(memory);
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniGuestCallDispatcher dispatcher(ledger);
    const auto get_version =
        *ogplay::runtime::FindJniSlot("GetVersion");
    std::uint32_t calls{};
    std::uint32_t vm_calls{};
    dispatcher.BindEnvironment(
        get_version,
        [&calls](const ogplay::runtime::JniGuestCallFrame& frame) {
            ++calls;
            CHECK_FALSE(frame.thunk.java_vm);
            CHECK(frame.thunk.slot ==
                  ogplay::runtime::FindJniSlot("GetVersion")->Value());
            CHECK(frame.thread_id == 101U);
            CHECK(frame.registers[0] ==
                  ogplay::runtime::kJniGuestEnvironment.Value());
            CHECK(frame.registers[1] == 0xaabbccddU);
            CHECK(frame.stack_pointer ==
                  ogplay::memory::GuestAddress{0x72001000U});
            return ogplay::runtime::JniGuestCallResult{
                ogplay::runtime::JniGuestReturnWidth::word,
                {static_cast<std::uint32_t>(
                     ogplay::runtime::kJniVersion1_6),
                 0U}};
        });
    const auto get_env =
        *ogplay::runtime::FindJniInvokeSlot("GetEnv");
    dispatcher.BindJavaVm(
        get_env,
        [&vm_calls](const ogplay::runtime::JniGuestCallFrame& frame) {
            ++vm_calls;
            CHECK(frame.thunk.java_vm);
            CHECK(frame.thunk.slot ==
                  ogplay::runtime::FindJniInvokeSlot("GetEnv")
                      ->Value());
            CHECK(frame.registers[0] ==
                  ogplay::runtime::kJniGuestJavaVm.Value());
            CHECK(frame.registers[1] == 0x72002000U);
            CHECK(frame.registers[2] ==
                  static_cast<std::uint32_t>(
                      ogplay::runtime::kJniVersion1_6));
            return ogplay::runtime::JniGuestCallResult{
                ogplay::runtime::JniGuestReturnWidth::word,
                {static_cast<std::uint32_t>(
                     ogplay::runtime::JniStatus::detached),
                 0U}};
        });
    dispatcher.Seal();

    const auto target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    get_version.Value() * sizeof(std::uint32_t)));
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(101U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.Environment().Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1, 0xaabbccddU);
    state.SetRegister(ogplay::cpu::CoreRegister::sp, 0x72001000U);
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x12345679U);
    cpu.SetState(state);

    const auto stopped = cpu.Run(1);
    REQUIRE(stopped.reason ==
            ogplay::cpu::RunStopReason::supervisor_call);
    CHECK(dispatcher.Handle(cpu, stopped));
    CHECK(calls == 1U);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) ==
          static_cast<std::uint32_t>(ogplay::runtime::kJniVersion1_6));
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r1) ==
          0xaabbccddU);

    const auto vm_target = Read32(
        memory, ogplay::runtime::kJniGuestInvokeTable.Add(
                    get_env.Value() * sizeof(std::uint32_t)));
    state = cpu.GetState();
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetRegister(ogplay::cpu::CoreRegister::pc,
                      vm_target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.JavaVm().Value());
    state.SetRegister(ogplay::cpu::CoreRegister::r1,
                      0x72002000U);
    state.SetRegister(
        ogplay::cpu::CoreRegister::r2,
        static_cast<std::uint32_t>(
            ogplay::runtime::kJniVersion1_6));
    cpu.SetState(state);
    const auto vm_stopped = cpu.Run(1);
    REQUIRE(dispatcher.Handle(cpu, vm_stopped));
    CHECK(vm_calls == 1U);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) ==
          static_cast<std::uint32_t>(
              ogplay::runtime::JniStatus::detached));
}

TEST_CASE("guest JNI dispatcher writes double-word returns and preserves void") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::cpu::InterpreterCpu cpu(bus);
    const ogplay::runtime::GuestJniAbi abi(memory);
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniGuestCallDispatcher dispatcher(ledger);
    const auto long_call =
        *ogplay::runtime::FindJniSlot("CallLongMethod");
    dispatcher.BindEnvironment(
        long_call, [](const ogplay::runtime::JniGuestCallFrame&) {
            return ogplay::runtime::JniGuestCallResult{
                ogplay::runtime::JniGuestReturnWidth::double_word,
                {0x01234567U, 0x89abcdefU}};
        });
    dispatcher.Seal();
    const auto target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    long_call.Value() * sizeof(std::uint32_t)));
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(102U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.Environment().Value());
    cpu.SetState(state);
    const auto stopped = cpu.Run(1);
    REQUIRE(dispatcher.Handle(cpu, stopped));
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) ==
          0x01234567U);
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r1) ==
          0x89abcdefU);

    ogplay::runtime::JniGuestCallDispatcher void_dispatcher(ledger);
    const auto delete_local =
        *ogplay::runtime::FindJniSlot("DeleteLocalRef");
    void_dispatcher.BindEnvironment(
        delete_local, [](const ogplay::runtime::JniGuestCallFrame&) {
            return ogplay::runtime::JniGuestCallResult{};
        });
    void_dispatcher.Seal();
    const auto void_target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    delete_local.Value() * sizeof(std::uint32_t)));
    state = cpu.GetState();
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetRegister(ogplay::cpu::CoreRegister::pc,
                      void_target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.Environment().Value());
    cpu.SetState(state);
    const auto void_stop = cpu.Run(1);
    REQUIRE(void_dispatcher.Handle(cpu, void_stop));
    CHECK(cpu.GetState().Register(ogplay::cpu::CoreRegister::r0) ==
          abi.Environment().Value());
}

TEST_CASE("guest JNI dispatcher fails closed on receiver and missing slots") {
    ogplay::memory::AddressSpace memory;
    ogplay::memory::CheckedMemoryBus bus(memory);
    ogplay::cpu::InterpreterCpu cpu(bus);
    const ogplay::runtime::GuestJniAbi abi(memory);
    ogplay::core::CapabilityLedger ledger;
    ogplay::runtime::JniGuestCallDispatcher dispatcher(ledger);
    const auto get_version =
        *ogplay::runtime::FindJniSlot("GetVersion");
    const auto target = Read32(
        memory, ogplay::runtime::kJniGuestEnvironmentTable.Add(
                    get_version.Value() * sizeof(std::uint32_t)));
    ogplay::cpu::A32State state;
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetThreadId(103U);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.Environment().Value());
    state.SetRegister(ogplay::cpu::CoreRegister::lr, 0x55667789U);
    cpu.SetState(state);
    dispatcher.Seal();
    const auto stopped = cpu.Run(1);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(dispatcher.Handle(cpu, stopped)),
        "unbound JNI guest slot: GetVersion",
        ogplay::runtime::JniGuestDispatchError);
    const auto hits = ledger.Unimplemented();
    REQUIRE(hits.size() == 1U);
    CHECK(hits[0].id == "runtime.jni.slot.GetVersion");
    CHECK(hits[0].first_lr == 0x55667789U);

    ogplay::runtime::JniGuestCallDispatcher receiver_dispatcher(ledger);
    receiver_dispatcher.BindEnvironment(
        get_version,
        [](const ogplay::runtime::JniGuestCallFrame&) {
            return ogplay::runtime::JniGuestCallResult{};
        });
    receiver_dispatcher.Seal();
    state = cpu.GetState();
    state.SetState(ogplay::cpu::ExecutionState::thumb);
    state.SetRegister(ogplay::cpu::CoreRegister::pc, target & ~1U);
    state.SetRegister(ogplay::cpu::CoreRegister::r0,
                      abi.JavaVm().Value());
    cpu.SetState(state);
    const auto invalid_receiver = cpu.Run(1);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            receiver_dispatcher.Handle(cpu, invalid_receiver)),
        "JNI guest call has an invalid interface receiver",
        ogplay::runtime::JniGuestDispatchError);

    const ogplay::cpu::RunResult unknown{
        1U, ogplay::cpu::RunStopReason::supervisor_call,
        ogplay::memory::GuestAddress{0x70000000U}, 0U, 3U,
        std::nullopt};
    CHECK_FALSE(receiver_dispatcher.Handle(cpu, unknown));

    ogplay::runtime::JniGuestCallDispatcher contracts(ledger);
    CHECK_THROWS_AS(
        contracts.BindEnvironment(
            ogplay::runtime::JniSlot{0U},
            [](const ogplay::runtime::JniGuestCallFrame&) {
                return ogplay::runtime::JniGuestCallResult{};
            }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        contracts.BindEnvironment(get_version, {}),
        std::invalid_argument);
    contracts.BindEnvironment(
        get_version,
        [](const ogplay::runtime::JniGuestCallFrame&) {
            return ogplay::runtime::JniGuestCallResult{};
        });
    CHECK_THROWS_AS(
        contracts.BindEnvironment(
            get_version,
            [](const ogplay::runtime::JniGuestCallFrame&) {
                return ogplay::runtime::JniGuestCallResult{};
            }),
        std::logic_error);
    contracts.Seal();
    CHECK(contracts.IsSealed());
    CHECK_THROWS_AS(contracts.Seal(), std::logic_error);
    CHECK_THROWS_AS(
        contracts.BindJavaVm(
            *ogplay::runtime::FindJniInvokeSlot("GetEnv"),
            [](const ogplay::runtime::JniGuestCallFrame&) {
                return ogplay::runtime::JniGuestCallResult{};
            }),
        std::logic_error);
}
