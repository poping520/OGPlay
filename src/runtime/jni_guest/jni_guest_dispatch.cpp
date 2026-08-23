#include "ogplay/runtime/jni_guest/jni_guest_dispatch.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace ogplay::runtime {
namespace {

[[nodiscard]] std::size_t EnvironmentIndex(const JniSlot slot) {
    const auto index = static_cast<std::size_t>(slot.Value());
    static_cast<void>(JniSlotName(slot));
    if (index < kJniReservedSlotCount) {
        throw std::invalid_argument(
            "reserved JNI guest slots cannot be bound");
    }
    return index;
}

[[nodiscard]] std::size_t JavaVmIndex(const JniInvokeSlot slot) {
    const auto index = static_cast<std::size_t>(slot.Value());
    static_cast<void>(JniInvokeSlotName(slot));
    if (index < 3U) {
        throw std::invalid_argument(
            "reserved JavaVM guest slots cannot be bound");
    }
    return index;
}

[[nodiscard]] std::string CapabilityId(const JniGuestThunk& thunk) {
    if (thunk.java_vm) {
        return "runtime.jni.invoke." +
               std::string(JniInvokeSlotName(JniInvokeSlot{
                   static_cast<std::uint8_t>(thunk.slot)}));
    }
    return JniCapabilityId(JniSlot{thunk.slot});
}

[[nodiscard]] std::string SlotName(const JniGuestThunk& thunk) {
    if (thunk.java_vm) {
        return std::string(JniInvokeSlotName(JniInvokeSlot{
            static_cast<std::uint8_t>(thunk.slot)}));
    }
    return std::string(JniSlotName(JniSlot{thunk.slot}));
}

}  // namespace

JniGuestCallDispatcher::JniGuestCallDispatcher(
    core::CapabilityLedger& ledger) noexcept
    : ledger_(&ledger) {}

void JniGuestCallDispatcher::BindEnvironment(
    const JniSlot slot, JniGuestCallHandler handler) {
    if (sealed_) {
        throw std::logic_error("JNI guest dispatcher is sealed");
    }
    const auto index = EnvironmentIndex(slot);
    if (!handler) {
        throw std::invalid_argument(
            "JNI guest environment handler cannot be empty");
    }
    if (environment_[index].has_value()) {
        throw std::logic_error(
            "JNI guest environment slot is already bound");
    }
    environment_[index] = std::move(handler);
}

void JniGuestCallDispatcher::BindJavaVm(
    const JniInvokeSlot slot, JniGuestCallHandler handler) {
    if (sealed_) {
        throw std::logic_error("JNI guest dispatcher is sealed");
    }
    const auto index = JavaVmIndex(slot);
    if (!handler) {
        throw std::invalid_argument(
            "JNI guest JavaVM handler cannot be empty");
    }
    if (java_vm_[index].has_value()) {
        throw std::logic_error(
            "JNI guest JavaVM slot is already bound");
    }
    java_vm_[index] = std::move(handler);
}

bool JniGuestCallDispatcher::IsEnvironmentBound(
    const JniSlot slot) const {
    return environment_[EnvironmentIndex(slot)].has_value();
}

bool JniGuestCallDispatcher::IsJavaVmBound(
    const JniInvokeSlot slot) const {
    return java_vm_[JavaVmIndex(slot)].has_value();
}

void JniGuestCallDispatcher::Seal() {
    if (sealed_) {
        throw std::logic_error("JNI guest dispatcher is already sealed");
    }
    sealed_ = true;
}

bool JniGuestCallDispatcher::Handle(
    cpu::Cpu& cpu, const cpu::RunResult& stopped) const {
    if (stopped.reason != cpu::RunStopReason::supervisor_call ||
        stopped.immediate != 3U ||
        stopped.pc.Value() ==
            std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto thunk = DescribeJniGuestThunk(
        memory::GuestAddress{stopped.pc.Value() + 1U});
    if (!thunk.has_value()) return false;
    if (!sealed_) {
        throw std::logic_error("JNI guest dispatcher is not sealed");
    }

    auto state = cpu.GetState();
    auto registers = state.CoreRegisters();
    Dispatch(*thunk, state.ThreadId(), registers);
    state.SetCoreRegisters(registers);
    cpu.SetState(state);
    return true;
}

cpu::HostCallResult JniGuestCallDispatcher::TryFastCall(
    cpu::A32HostCallContext& call) const noexcept {
    const auto thunk = DescribeJniGuestThunk(
        memory::GuestAddress{call.pc.Value() + 1U});
    if (!thunk.has_value()) return cpu::HostCallResult::unhandled;
    try {
        Dispatch(*thunk, call.thread_id, call.registers);
        return cpu::HostCallResult::handled;
    } catch (...) {
        return cpu::HostCallResult::fault;
    }
}

void JniGuestCallDispatcher::Dispatch(
    const JniGuestThunk& thunk, const std::uint64_t thread_id,
    const std::span<std::uint32_t, 16> registers) const {
    if (!sealed_) {
        throw std::logic_error("JNI guest dispatcher is not sealed");
    }
    if (thread_id == 0U) {
        throw JniGuestDispatchError(
            "JNI guest call requires a non-zero thread id");
    }
    const auto expected_receiver =
        thunk.java_vm ? kJniGuestJavaVm.Value()
                      : kJniGuestEnvironment.Value();
    if (registers[0] != expected_receiver) {
        throw JniGuestDispatchError(
            "JNI guest call has an invalid interface receiver");
    }

    const JniGuestCallHandler* handler{};
    if (thunk.java_vm) {
        const auto index = JavaVmIndex(JniInvokeSlot{
            static_cast<std::uint8_t>(thunk.slot)});
        handler = java_vm_[index] ? &*java_vm_[index] : nullptr;
    } else {
        const auto index = EnvironmentIndex(JniSlot{thunk.slot});
        handler =
            environment_[index] ? &*environment_[index] : nullptr;
    }
    if (handler == nullptr) {
        ledger_->RecordUnimplemented(CapabilityId(thunk), registers[14]);
        throw JniGuestDispatchError(
            "unbound JNI guest slot: " + SlotName(thunk));
    }

    JniGuestCallFrame frame;
    frame.thunk = thunk;
    frame.thread_id = thread_id;
    frame.link_register = registers[14];
    frame.stack_pointer = memory::GuestAddress{registers[13]};
    for (std::size_t index = 0; index < frame.registers.size(); ++index) {
        frame.registers[index] = registers[index];
    }
    const auto result = (*handler)(frame);
    if (result.width != JniGuestReturnWidth::none) {
        registers[0] = result.words[0];
    }
    if (result.width == JniGuestReturnWidth::double_word) {
        registers[1] = result.words[1];
    }
}

}  // namespace ogplay::runtime
