#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "ogplay/cpu/cpu.h"
#include "ogplay/memory/bus.h"

namespace ogplay::cpu {

class DynarmicCpu final : public Cpu {
public:
    explicit DynarmicCpu(memory::MemoryBus& memory_bus);
    ~DynarmicCpu() override;

    DynarmicCpu(const DynarmicCpu&) = delete;
    DynarmicCpu& operator=(const DynarmicCpu&) = delete;

    [[nodiscard]] RunResult Run(std::uint64_t tick_budget) override;
    [[nodiscard]] A32State GetState() const override;
    void SetState(const A32State& state) override;
    void RequestHalt() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic_bool halt_requested_{false};
};

}  // namespace ogplay::cpu
