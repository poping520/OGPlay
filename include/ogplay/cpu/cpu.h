#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "ogplay/memory/address_space.h"

namespace ogplay::cpu {

struct A32HostCallContext final {
    std::span<std::uint32_t, 16> registers;
    std::uint64_t thread_id{};
    memory::GuestAddress pc;
};

enum class HostCallResult : std::uint8_t { handled, unhandled, fault };

using HostCallFn = HostCallResult (*)(void* userdata, std::uint32_t svc,
                                      A32HostCallContext& call) noexcept;

struct HostCallHook final {
    HostCallFn invoke{};
    void* userdata{};
};

enum class CoreRegister : std::uint8_t {
    r0,
    r1,
    r2,
    r3,
    r4,
    r5,
    r6,
    r7,
    r8,
    r9,
    r10,
    r11,
    r12,
    sp,
    lr,
    pc,
};

enum class ExecutionState : std::uint8_t { a32, thumb };

// Diagnostic names for guest-stop reports; callers append the numeric value
// so historical numeric workflows keep matching.
[[nodiscard]] constexpr std::string_view ToString(
    const ExecutionState state) noexcept {
    switch (state) {
        case ExecutionState::a32:
            return "a32";
        case ExecutionState::thumb:
            return "thumb";
    }
    return "unknown";
}

class A32State final {
public:
    A32State();

    [[nodiscard]] std::uint32_t Register(CoreRegister index) const;
    void SetRegister(CoreRegister index, std::uint32_t value);
    [[nodiscard]] const std::array<std::uint32_t, 16>& CoreRegisters() const noexcept;
    void SetCoreRegisters(
        const std::array<std::uint32_t, 16>& values) noexcept;
    [[nodiscard]] const std::array<std::uint32_t, 64>& ExtendedRegisters() const noexcept;
    void SetExtendedRegister(std::uint8_t index, std::uint32_t value);
    void SetExtendedRegisters(
        const std::array<std::uint32_t, 64>& values) noexcept;
    [[nodiscard]] std::uint32_t Cpsr() const noexcept;
    void SetCpsr(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t Fpscr() const noexcept;
    void SetFpscr(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t Fpexc() const noexcept;
    void SetFpexc(std::uint32_t value) noexcept;
    [[nodiscard]] std::uint64_t ThreadId() const noexcept;
    void SetThreadId(std::uint64_t value) noexcept;
    [[nodiscard]] memory::GuestAddress ThreadPointer() const noexcept;
    void SetThreadPointer(memory::GuestAddress value) noexcept;
    [[nodiscard]] ExecutionState State() const noexcept;
    void SetState(ExecutionState state) noexcept;

    bool operator==(const A32State&) const = default;

private:
    std::array<std::uint32_t, 16> core_{};
    std::array<std::uint32_t, 64> extended_{};
    std::uint32_t cpsr_{};
    std::uint32_t fpscr_{};
    std::uint32_t fpexc_{};
    std::uint64_t thread_id_{};
    memory::GuestAddress thread_pointer_{};
};

inline constexpr std::uint32_t kCpuSnapshotVersion = 2;

struct CpuSnapshot final {
    std::uint32_t version{kCpuSnapshotVersion};
    A32State state;
};

enum class RunStopReason : std::uint8_t {
    budget_exhausted,
    supervisor_call,
    breakpoint,
    undefined_instruction,
    memory_fault,
    halt_requested,
    host_call_fault,
};

[[nodiscard]] constexpr std::string_view ToString(
    const RunStopReason reason) noexcept {
    switch (reason) {
        case RunStopReason::budget_exhausted:
            return "budget_exhausted";
        case RunStopReason::supervisor_call:
            return "supervisor_call";
        case RunStopReason::breakpoint:
            return "breakpoint";
        case RunStopReason::undefined_instruction:
            return "undefined_instruction";
        case RunStopReason::memory_fault:
            return "memory_fault";
        case RunStopReason::halt_requested:
            return "halt_requested";
        case RunStopReason::host_call_fault:
            return "host_call_fault";
    }
    return "unknown";
}

struct CpuFault final {
    memory::GuestAddress address;
    memory::AccessType access;
    memory::FaultReason reason;
    std::uint64_t thread_id{};
};

struct RunResult final {
    std::uint64_t ticks_consumed{};
    RunStopReason reason{RunStopReason::budget_exhausted};
    memory::GuestAddress pc;
    std::uint32_t instruction{};
    std::uint32_t immediate{};
    std::optional<CpuFault> fault;
};

class Cpu {
public:
    virtual ~Cpu() = default;
    [[nodiscard]] virtual RunResult Run(std::uint64_t tick_budget) = 0;
    [[nodiscard]] virtual A32State GetState() const = 0;
    virtual void SetState(const A32State& state) = 0;
    virtual void RequestHalt() noexcept = 0;
    virtual void SetHostCallHook(HostCallHook hook) noexcept {
        static_cast<void>(hook);
    }
};

}  // namespace ogplay::cpu
