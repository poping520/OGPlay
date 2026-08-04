#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "ogplay/cpu/cpu.h"
#include "ogplay/memory/bus.h"

namespace ogplay::cpu {

class DynarmicExecutionContext final {
public:
    explicit DynarmicExecutionContext(std::size_t maximum_processors);
    ~DynarmicExecutionContext();

    DynarmicExecutionContext(const DynarmicExecutionContext&) = delete;
    DynarmicExecutionContext& operator=(const DynarmicExecutionContext&) = delete;

private:
    friend class DynarmicCpu;
    [[nodiscard]] std::size_t AcquireProcessor();
    void ReleaseProcessor(std::size_t processor_id) noexcept;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

class DynarmicCpu final : public Cpu {
public:
    explicit DynarmicCpu(memory::MemoryBus& memory_bus);
    DynarmicCpu(memory::MemoryBus& memory_bus,
                std::shared_ptr<DynarmicExecutionContext> context);
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
