#include "runtime/integration/nested_guest_cpu_pool.h"

#include <utility>

namespace ogplay::runtime::detail {

NestedGuestCpuPool::NestedGuestCpuPool(
    memory::MemoryBus& memory_bus,
    std::shared_ptr<cpu::DynarmicExecutionContext> execution_context,
    Configure configure)
    : memory_bus_(memory_bus),
      execution_context_(std::move(execution_context)),
      configure_(std::move(configure)) {}

cpu::DynarmicCpu& NestedGuestCpuPool::Acquire(
    const std::uint64_t thread_id, const std::size_t depth,
    const cpu::A32State& initial_state) {
    const std::scoped_lock lock(mutex_);
    auto& pool = pools_[thread_id];
    const bool reuse = depth < pool.size();
    while (pool.size() <= depth) {
        auto instance = std::make_unique<cpu::DynarmicCpu>(
            memory_bus_, execution_context_);
        if (configure_) configure_(*instance);
        pool.push_back(std::move(instance));
    }
    if (reuse) ++reuses_;
    pool[depth]->SetState(initial_state);
    return *pool[depth];
}

void NestedGuestCpuPool::ReleaseThread(const std::uint64_t thread_id) noexcept {
    const std::scoped_lock lock(mutex_);
    pools_.erase(thread_id);
}

NestedGuestCpuPoolStats NestedGuestCpuPool::Stats() const noexcept {
    const std::scoped_lock lock(mutex_);
    std::size_t instances{};
    for (const auto& [thread_id, pool] : pools_) {
        static_cast<void>(thread_id);
        instances += pool.size();
    }
    return {instances, reuses_};
}

}  // namespace ogplay::runtime::detail
