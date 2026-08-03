#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "ogplay/cpu/cpu.h"
#include "ogplay/memory/bus.h"

namespace ogplay::cpu {

class InterpreterCpu final : public Cpu {
public:
    explicit InterpreterCpu(memory::MemoryBus& memory_bus) noexcept;

    [[nodiscard]] RunResult Run(std::uint64_t tick_budget) override;
    [[nodiscard]] A32State GetState() const override;
    void SetState(const A32State& state) override;
    void RequestHalt() noexcept override;

private:
    [[nodiscard]] std::optional<RunResult> ExecuteA32(memory::GuestAddress pc,
                                                      std::uint32_t instruction);
    [[nodiscard]] std::optional<RunResult> ExecuteThumb(memory::GuestAddress pc,
                                                        std::uint16_t instruction);

    memory::MemoryBus& memory_bus_;
    A32State state_;
    std::atomic_bool halt_requested_{false};
};

}  // namespace ogplay::cpu
