#include "ogplay/cpu/thread_group.h"

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ogplay/hal/thread.h"

namespace ogplay::cpu {

class GuestThreadGroup::Impl final {
public:
    struct Record final {
        GuestThreadStart start;
        std::unique_ptr<hal::HostThread> host_thread;
        std::mutex mutex;
        A32State final_state;
        std::exception_ptr failure;
        bool joined{};
    };

    explicit Impl(CpuFactory factory) : factory_(std::move(factory)) {
        if (!factory_) throw std::invalid_argument("CPU factory is empty");
    }

    ~Impl() {
        decltype(records_) retired;
        { std::scoped_lock lock(records_mutex_); retired.swap(records_); }
        // Joining workers can invoke callbacks; never join under the registry lock.
        for (const auto& [id, record] : retired) {
            static_cast<void>(id);
            if (record->host_thread->Joinable()) record->host_thread->Join();
        }
    }

    void Spawn(GuestThreadStart start, GuestThreadEntry entry, GuestThreadFailureHandler failure_handler) {
        if (start.thread_id == 0) {
            throw std::invalid_argument("guest thread id must be non-zero");
        }
        if (!entry) throw std::invalid_argument("guest thread entry is empty");
        if (start.cpu_state.ThreadId() != 0 &&
            start.cpu_state.ThreadId() != start.thread_id) {
            throw std::invalid_argument("CPU state thread id does not match start record");
        }
        if (start.cpu_state.ThreadPointer().Value() != 0 &&
            start.cpu_state.ThreadPointer() != start.tls_base) {
            throw std::invalid_argument(
                "CPU state thread pointer does not match TLS base");
        }
        start.cpu_state.SetThreadId(start.thread_id);
        start.cpu_state.SetThreadPointer(start.tls_base);
        auto record = std::make_shared<Record>();
        record->start = std::move(start);

        std::scoped_lock lock(records_mutex_);
        if (records_.contains(record->start.thread_id)) {
            throw std::invalid_argument("duplicate guest thread id");
        }
        record->host_thread = hal::StartHostThread(
            [this, record, entry = std::move(entry), failure_handler = std::move(failure_handler)]() mutable {
                active_count_.fetch_add(1);
                try {
                    auto cpu = factory_();
                    if (!cpu) throw std::runtime_error("CPU factory returned null");
                    cpu->SetState(record->start.cpu_state);
                    entry(*cpu);
                    std::scoped_lock record_lock(record->mutex);
                    record->final_state = cpu->GetState();
                } catch (...) {
                    const auto failure = std::current_exception();
                    { std::scoped_lock record_lock(record->mutex); record->failure = failure; }
                    if (failure_handler) {
                        try { failure_handler(failure); } catch (...) { /* Keep original failure. */ }
                    }
                }
                active_count_.fetch_sub(1);
            });
        records_.emplace(record->start.thread_id, std::move(record));
    }

    GuestThreadExit Join(const std::uint64_t thread_id) {
        std::shared_ptr<Record> record;
        {
            std::scoped_lock lock(records_mutex_);
            const auto iterator = records_.find(thread_id);
            if (iterator == records_.end()) {
                throw std::out_of_range("unknown guest thread id");
            }
            record = iterator->second;
            if (record->joined) throw std::logic_error("guest thread already joined");
            record->joined = true;
        }
        record->host_thread->Join();
        std::scoped_lock record_lock(record->mutex);
        if (record->failure) std::rethrow_exception(record->failure);
        return {record->start.thread_id, record->start.tls_base,
                record->final_state};
    }

    std::size_t ThreadCount() const {
        std::scoped_lock lock(records_mutex_);
        return records_.size();
    }

    std::size_t ActiveCount() const noexcept { return active_count_.load(); }

private:
    CpuFactory factory_;
    mutable std::mutex records_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Record>> records_;
    std::atomic_size_t active_count_{};
};

GuestThreadGroup::GuestThreadGroup(CpuFactory cpu_factory)
    : impl_(std::make_unique<Impl>(std::move(cpu_factory))) {}

GuestThreadGroup::~GuestThreadGroup() = default;

void GuestThreadGroup::Spawn(GuestThreadStart start, GuestThreadEntry entry,
                             GuestThreadFailureHandler failure_handler) {
    impl_->Spawn(std::move(start), std::move(entry), std::move(failure_handler));
}

GuestThreadExit GuestThreadGroup::Join(const std::uint64_t thread_id) {
    return impl_->Join(thread_id);
}

std::size_t GuestThreadGroup::ThreadCount() const { return impl_->ThreadCount(); }
std::size_t GuestThreadGroup::ActiveCount() const noexcept {
    return impl_->ActiveCount();
}

}  // namespace ogplay::cpu
