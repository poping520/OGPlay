#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/jni_guest/jni_guest_abi.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_java_vm.h"

namespace ogplay::runtime {

enum class JniGuestReturnWidth : std::uint8_t {
    none,
    word,
    double_word,
};

struct JniGuestCallResult final {
    JniGuestReturnWidth width{JniGuestReturnWidth::none};
    std::array<std::uint32_t, 2> words{};
};

struct JniGuestCallFrame final {
    JniGuestThunk thunk;
    std::uint64_t thread_id{};
    std::uint32_t link_register{};
    std::array<std::uint32_t, 4> registers{};
    memory::GuestAddress stack_pointer;
};

using JniGuestCallHandler =
    std::function<JniGuestCallResult(const JniGuestCallFrame&)>;

class JniGuestDispatchError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class JniGuestCallDispatcher final {
public:
    explicit JniGuestCallDispatcher(
        core::CapabilityLedger& ledger) noexcept;

    void BindEnvironment(JniSlot slot, JniGuestCallHandler handler);
    void BindJavaVm(JniInvokeSlot slot, JniGuestCallHandler handler);
    [[nodiscard]] bool IsEnvironmentBound(JniSlot slot) const;
    [[nodiscard]] bool IsJavaVmBound(JniInvokeSlot slot) const;
    void Seal();
    [[nodiscard]] bool IsSealed() const noexcept { return sealed_; }

    [[nodiscard]] bool Handle(cpu::Cpu& cpu,
                              const cpu::RunResult& stopped) const;
    [[nodiscard]] cpu::HostCallResult TryFastCall(
        cpu::A32HostCallContext& call) const noexcept;

private:
    struct PendingFault final {
        std::uint64_t thread_id{};
        memory::GuestAddress pc{};
        std::exception_ptr exception;
    };

    void RecordFastFault(const cpu::A32HostCallContext& call,
                         std::exception_ptr exception) const noexcept;
    [[nodiscard]] std::exception_ptr TakeFastFault(
        std::uint64_t thread_id, memory::GuestAddress pc) const noexcept;
    void Dispatch(const JniGuestThunk& thunk, std::uint64_t thread_id,
                  std::span<std::uint32_t, 16> registers) const;
    core::CapabilityLedger* ledger_{};
    std::array<std::optional<JniGuestCallHandler>,
               kJniNativeInterfaceSlotCount>
        environment_{};
    std::array<std::optional<JniGuestCallHandler>,
               kJniInvokeInterfaceSlotCount>
        java_vm_{};
    bool sealed_{};
    mutable std::mutex pending_fault_mutex_;
    mutable std::vector<PendingFault> pending_faults_;
};

}  // namespace ogplay::runtime
