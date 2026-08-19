#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/dexvm_types.h"

namespace ogplay::runtime::dexvm {

enum class DexVmTraceKind : std::uint8_t {
    instruction,
    method_enter,
    method_exit,
    exception_throw,
    exception_catch,
    class_init_begin,
    class_init_end,
    class_init_fail,
    monitor_enter,
    monitor_exit,
    monitor_wait,
    monitor_notify,
    native_enter,
    native_exit,
    gc_begin,
    gc_end,
};

constexpr std::uint64_t DexVmTraceBit(const DexVmTraceKind kind) noexcept {
    return UINT64_C(1) << static_cast<std::uint8_t>(kind);
}

constexpr std::uint64_t kDexVmTraceAllEvents =
    (DexVmTraceBit(DexVmTraceKind::gc_end) << 1U) - 1U;

struct DexVmDiagnosticsConfig final {
    // Zero keeps the recorder disabled. The fixed ring is allocated once
    // during Interpreter construction and never grows on the guest hot path.
    std::size_t trace_capacity{};
    std::uint64_t event_mask{kDexVmTraceAllEvents};
    // Instruction events are sampled every N instructions. Other event
    // families are never sampled. Must be non-zero.
    std::uint32_t instruction_sample_interval{1};
};

struct DexVmTraceEntry final {
    std::uint64_t sequence{};
    DexVmTraceKind kind{DexVmTraceKind::instruction};
    std::uint64_t context_token{};
    std::uint64_t tick{};
    std::string class_descriptor;
    std::string method_name;
    std::string method_descriptor;
    std::uint32_t dex_pc{};
    std::uint8_t opcode{};
    std::uint64_t value{};
};

struct DexVmStackFrame final {
    std::string class_descriptor;
    std::string method_name;
    std::string method_descriptor;
    std::uint32_t dex_pc{};
};

struct DexVmThreadStack final {
    std::uint64_t context_token{};
    std::uint64_t guest_thread_id{};
    std::string thread_name;
    std::string thread_status;
    std::uint64_t ticks{};
    std::string pending_exception;
    std::vector<DexVmStackFrame> frames;
};

[[nodiscard]] std::string_view DexVmTraceKindName(
    DexVmTraceKind kind) noexcept;
[[nodiscard]] std::string RenderDexVmTraceJson(
    const std::vector<DexVmTraceEntry>& entries);
[[nodiscard]] std::string RenderDexVmStacksJson(
    const std::vector<DexVmThreadStack>& stacks);

}  // namespace ogplay::runtime::dexvm
