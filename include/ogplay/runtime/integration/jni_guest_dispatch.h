#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/cpu/cpu.h"
#include "ogplay/runtime/integration/jni_guest_abi.h"
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
    void Seal();
    [[nodiscard]] bool IsSealed() const noexcept { return sealed_; }

    [[nodiscard]] bool Handle(cpu::Cpu& cpu,
                              const cpu::RunResult& stopped) const;

private:
    core::CapabilityLedger* ledger_{};
    std::array<std::optional<JniGuestCallHandler>,
               kJniNativeInterfaceSlotCount>
        environment_{};
    std::array<std::optional<JniGuestCallHandler>,
               kJniInvokeInterfaceSlotCount>
        java_vm_{};
    bool sealed_{};
};

}  // namespace ogplay::runtime
