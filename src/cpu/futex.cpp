#include "ogplay/cpu/futex.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace ogplay::cpu {
namespace {

void ValidateAddress(const memory::GuestAddress address) {
    if (!address.IsAligned(sizeof(std::uint32_t))) {
        throw std::invalid_argument("futex address must be 32-bit aligned");
    }
}

struct WaitQueue final {
    std::mutex mutex;
    std::condition_variable wake;
    std::size_t waiters{};
    std::size_t wake_tokens{};
};

}  // namespace

class FutexTable::Impl final {
public:
    FutexWaitResult Wait(memory::MemoryBus& memory_bus,
                         const memory::GuestAddress address,
                         const std::uint32_t expected,
                         const std::uint64_t thread_id) {
        ValidateAddress(address);
        const auto queue = GetOrCreate(address);
        std::unique_lock lock(queue->mutex);
        if (memory_bus.Read32(address, thread_id) != expected) {
            return FutexWaitResult::value_mismatch;
        }
        if (interrupted_.load()) return FutexWaitResult::interrupted;
        ++queue->waiters;
        queue->wake.wait(lock, [this, &queue] {
            return interrupted_.load() || queue->wake_tokens != 0;
        });
        if (interrupted_.load()) {
            --queue->waiters;
            return FutexWaitResult::interrupted;
        }
        --queue->wake_tokens;
        --queue->waiters;
        return FutexWaitResult::awoken;
    }

    std::size_t Wake(const memory::GuestAddress address,
                     const std::size_t maximum_count) {
        ValidateAddress(address);
        const auto queue = Find(address);
        if (!queue || maximum_count == 0) return 0;
        std::scoped_lock lock(queue->mutex);
        if (interrupted_.load()) return 0;
        const auto available = queue->waiters - queue->wake_tokens;
        const auto count = std::min(maximum_count, available);
        queue->wake_tokens += count;
        for (std::size_t index = 0; index < count; ++index) {
            queue->wake.notify_one();
        }
        return count;
    }

    std::size_t WakeAll() {
        if (interrupted_.load()) return 0;
        std::vector<std::shared_ptr<WaitQueue>> queues;
        {
            std::scoped_lock lock(queues_mutex_);
            queues.reserve(queues_.size());
            for (const auto& entry : queues_) queues.push_back(entry.second);
        }
        std::size_t total{};
        for (const auto& queue : queues) {
            std::scoped_lock lock(queue->mutex);
            if (interrupted_.load()) continue;
            const auto count = queue->waiters - queue->wake_tokens;
            queue->wake_tokens += count;
            total += count;
            queue->wake.notify_all();
        }
        return total;
    }

    std::size_t InterruptAll() {
        interrupted_.store(true);
        std::vector<std::shared_ptr<WaitQueue>> queues;
        {
            std::scoped_lock lock(queues_mutex_);
            queues.reserve(queues_.size());
            for (const auto& entry : queues_) queues.push_back(entry.second);
        }
        std::size_t total{};
        for (const auto& queue : queues) {
            std::scoped_lock lock(queue->mutex);
            total += queue->waiters;
            queue->wake.notify_all();
        }
        return total;
    }

    std::size_t WaiterCount(const memory::GuestAddress address) const {
        ValidateAddress(address);
        const auto queue = Find(address);
        if (!queue) return 0;
        std::scoped_lock lock(queue->mutex);
        return queue->waiters;
    }

private:
    std::shared_ptr<WaitQueue> GetOrCreate(const memory::GuestAddress address) {
        std::scoped_lock lock(queues_mutex_);
        auto& queue = queues_[address.Value()];
        if (!queue) queue = std::make_shared<WaitQueue>();
        return queue;
    }

    std::shared_ptr<WaitQueue> Find(const memory::GuestAddress address) const {
        std::scoped_lock lock(queues_mutex_);
        const auto iterator = queues_.find(address.Value());
        return iterator == queues_.end() ? nullptr : iterator->second;
    }

    mutable std::mutex queues_mutex_;
    std::unordered_map<std::uint32_t, std::shared_ptr<WaitQueue>> queues_;
    std::atomic_bool interrupted_{};
};

FutexTable::FutexTable() : impl_(std::make_unique<Impl>()) {}
FutexTable::~FutexTable() = default;

FutexWaitResult FutexTable::Wait(memory::MemoryBus& memory_bus,
                                 const memory::GuestAddress address,
                                 const std::uint32_t expected,
                                 const std::uint64_t thread_id) {
    return impl_->Wait(memory_bus, address, expected, thread_id);
}

std::size_t FutexTable::Wake(const memory::GuestAddress address,
                             const std::size_t maximum_count) {
    return impl_->Wake(address, maximum_count);
}

std::size_t FutexTable::WakeAll() { return impl_->WakeAll(); }

std::size_t FutexTable::InterruptAll() { return impl_->InterruptAll(); }

std::size_t FutexTable::WaiterCount(const memory::GuestAddress address) const {
    return impl_->WaiterCount(address);
}

}  // namespace ogplay::cpu
