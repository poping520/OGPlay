#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ogplay/cpu/dynarmic.h"

namespace ogplay::runtime::detail {

struct NestedGuestCpuPoolStats final {
    std::size_t instances{};
    std::uint64_t reuses{};
};

// Dynarmic cannot recursively enter an executor whose Run() is suspended in a
// host callback. One executor per guest thread and nesting depth keeps state
// isolated while retaining translated code across repeated JNI reentry.
class NestedGuestCpuPool final {
public:
    using Configure = std::function<void(cpu::DynarmicCpu&)>;

    NestedGuestCpuPool(
        memory::MemoryBus& memory_bus,
        std::shared_ptr<cpu::DynarmicExecutionContext> execution_context,
        Configure configure = {});

    [[nodiscard]] cpu::DynarmicCpu& Acquire(
        std::uint64_t thread_id, std::size_t depth,
        const cpu::A32State& initial_state);
    void ReleaseThread(std::uint64_t thread_id) noexcept;
    [[nodiscard]] NestedGuestCpuPoolStats Stats() const noexcept;

private:
    memory::MemoryBus& memory_bus_;
    std::shared_ptr<cpu::DynarmicExecutionContext> execution_context_;
    Configure configure_;
    mutable std::mutex mutex_;
    std::unordered_map<
        std::uint64_t, std::vector<std::unique_ptr<cpu::DynarmicCpu>>>
        pools_;
    std::uint64_t reuses_{};
};

}  // namespace ogplay::runtime::detail
