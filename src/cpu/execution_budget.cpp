#include "ogplay/cpu/execution_budget.h"
#include "ogplay/hal/clock.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace ogplay::cpu {
namespace {
constexpr std::uint64_t kSliceTicks = 50000;
constexpr std::uint64_t kNsPerSecond = 1000000000;
}

class ExecutionBudget::Impl final {
public:
    Impl(const ExecutionBudgetConfig requested, NowFunction source)
        : config(requested), now(source ? std::move(source) : hal::Clock::SteadyTimestampNs),
          // A 50 ms bucket tolerates coarse host timer wakeups. Individual
          // slices use at most half the bucket, retaining refill headroom.
          capacity(config.ticks_per_second == 0 ? kSliceTicks :
              std::max(config.ticks_per_second / 20, UINT64_C(1))),
          tokens(capacity), last_ns(now()) {
        if (config.maximum_parallelism > 32 || config.ticks_per_second > UINT64_C(10000000000))
            throw std::invalid_argument("CPU execution budget exceeds supported limits");
    }
    void Refill() {
        const auto current = now();
        if (current <= last_ns) return;
        const auto elapsed = current - last_ns;
        last_ns = current;
        if (elapsed >= kNsPerSecond) {
            tokens = capacity;
            fraction = 0;
            return;
        }
        // elapsed < 1e9, rate <= 1e10: product fits uint64_t on MSVC too.
        const auto accrued = elapsed * config.ticks_per_second + fraction;
        tokens = std::min(capacity, tokens + accrued / kNsPerSecond);
        fraction = tokens == capacity ? 0 : accrued % kNsPerSecond;
    }
    const ExecutionBudgetConfig config;
    const NowFunction now;
    const std::uint64_t capacity;
    std::uint64_t tokens{}, last_ns{}, fraction{}, next_ticket{};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::uint64_t> queue;
    ExecutionBudgetSnapshot stats;
};

ExecutionBudget::ExecutionBudget(const ExecutionBudgetConfig config, NowFunction now)
    : impl_(std::make_unique<Impl>(config, std::move(now))) {}
ExecutionBudget::~ExecutionBudget() = default;

ExecutionBudget::Lease::Lease(Lease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), ticks_(other.ticks_) {}
ExecutionBudget::Lease::~Lease() { if (owner_) owner_->Release(ticks_, 0); }
void ExecutionBudget::Lease::Complete(const std::uint64_t consumed) {
    if (!owner_) throw std::logic_error("CPU execution lease already completed");
    std::exchange(owner_, nullptr)->Release(ticks_, std::min(ticks_, consumed));
    if (consumed > ticks_) throw std::logic_error("CPU exceeded its execution lease");
}

ExecutionBudget::Lease ExecutionBudget::Acquire(const std::uint64_t requested,
                                                const std::atomic_bool& canceled) {
    if (requested == 0) return {};
    auto& s = *impl_;
    std::unique_lock lock(s.mutex);
    const auto ticket = s.next_ticket++;
    s.queue.push_back(ticket);
    const auto remove = [&] {
        std::erase(s.queue, ticket);
        s.wake.notify_all();
    };
    const auto quantum = s.config.ticks_per_second == 0 ? kSliceTicks :
        std::min(kSliceTicks, std::max(s.capacity / 2, UINT64_C(1)));
    const auto ticks = std::min(requested, quantum);
    try {
        for (;;) {
            if (canceled.load(std::memory_order_acquire)) { remove(); return {}; }
            s.Refill();
            const bool lane = s.config.maximum_parallelism == 0 || s.stats.active < s.config.maximum_parallelism;
            const bool credit = s.config.ticks_per_second == 0 || s.tokens >= ticks;
            if (s.stats.draining || (s.queue.front() == ticket && lane && credit)) {
                remove();
                ++s.stats.active;
                s.stats.peak_active = std::max(s.stats.peak_active, s.stats.active);
                if (!s.stats.draining && s.config.ticks_per_second != 0) s.tokens -= ticks;
                return Lease(*this, ticks);
            }
            // Clock supplies all timestamps. This bounded host wait also
            // responds to cancellation without advancing guest time.
            s.wake.wait_for(lock, std::chrono::milliseconds(2));
        }
    } catch (...) { remove(); throw; }
}

void ExecutionBudget::Release(const std::uint64_t reserved, const std::uint64_t consumed) noexcept {
    auto& s = *impl_;
    std::scoped_lock lock(s.mutex);
    --s.stats.active;
    s.stats.consumed_ticks += consumed;
    s.tokens = std::min(s.capacity, s.tokens + reserved - consumed);
    s.wake.notify_all();
}
void ExecutionBudget::Notify() noexcept { impl_->wake.notify_all(); }
void ExecutionBudget::BeginDrain() noexcept {
    std::scoped_lock lock(impl_->mutex);
    impl_->stats.draining = true;
    impl_->wake.notify_all();
}
ExecutionBudgetSnapshot ExecutionBudget::Snapshot() const {
    std::scoped_lock lock(impl_->mutex);
    auto stats = impl_->stats;
    stats.waiting = static_cast<std::uint32_t>(impl_->queue.size());
    return stats;
}

}  // namespace ogplay::cpu
