#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <stdexcept>

#include "ogplay/hal/clock.h"

namespace ogplay::runtime {

std::int32_t AndroidModule::AddLooperFdLocked(memory::GuestAddress handle, std::int32_t fd, std::int32_t ident,
                                              std::uint32_t events, memory::GuestAddress callback,
                                              memory::GuestAddress data, std::function<void()> on_readable) {
    auto& looper = RequireLooper(handle);
    if (looper.retired || fd < 0 || !looper_hooks_.poll_events || (events & ~31U) != 0) return -1;
    if (callback.IsNull() && !on_readable && (ident < 0 || !looper.allow_non_callbacks)) return -1;
    if (!callback.IsNull() && !looper_hooks_.invoke_callback) return -1;
    const auto available = looper_hooks_.poll_events(fd);
    if (!available || (*available & 16U) != 0) return -1;
    looper.requests[fd] = {ident, events, data, callback, ++looper.generation, std::move(on_readable)};
    ready_.notify_all();
    return 1;
}

std::int32_t AndroidModule::RemoveLooperFdLocked(memory::GuestAddress looper, std::int32_t fd) {
    return static_cast<std::int32_t>(RequireLooper(looper).requests.erase(fd));
}

AndroidModule::Looper& AndroidModule::RequireLooper(memory::GuestAddress handle) {
    const auto found = loopers_.find(handle.Value());
    if (found == loopers_.end()) throw std::invalid_argument("ALooper is stale or unregistered");
    return found->second;
}

void AndroidModule::RetireThreadLooperLocked(std::uint64_t tid) {
    RetireSensorQueues(tid);
    const auto found = thread_loopers_.find(tid);
    if (found == thread_loopers_.end()) return;
    const auto handle = found->second;
    auto& looper = RequireLooper(handle);
    looper.retired = true;
    looper.requests.clear();
    if (--looper.references == 0) loopers_.erase(handle.Value());
    thread_loopers_.erase(found);
    if (standalone_input_.looper == handle) standalone_input_.looper = memory::GuestAddress{};
    for (auto& [_, resource] : resources_) {
        if (resource.input.looper != handle) continue;
        resource.input.looper = memory::GuestAddress{};
        resource.input_attached = false;
    }
    ready_.notify_all();
}

void AndroidModule::SweepLoopers() {
    if (!looper_hooks_.thread_running) return;
    for (auto it = sensor_queues_.begin(); it != sensor_queues_.end();) {
        const auto current = it++;
        if (!looper_hooks_.thread_running(current->second.owner)) {
            CloseSensorQueueLocked(current->second);
            sensor_queues_.erase(current);
        }
    }
    for (auto it = thread_loopers_.begin(); it != thread_loopers_.end();) {
        const auto tid = (it++)->first;
        if (!looper_hooks_.thread_running(tid)) RetireThreadLooperLocked(tid);
    }
}

memory::GuestAddress AndroidModule::PrepareThreadLooper(std::uint64_t tid, std::uint32_t opts) {
    std::scoped_lock lock(mutex_);
    SweepLoopers();
    if (loopers_shutdown_ || (looper_hooks_.thread_running && !looper_hooks_.thread_running(tid)))
        throw std::runtime_error("ALooper_prepare on a retired thread");
    if (const auto found = thread_loopers_.find(tid); found != thread_loopers_.end()) return found->second;
    if (next_looper_.Value() >= 0x6e100000U) throw std::runtime_error("ALooper handle budget exhausted");
    const auto handle = next_looper_;
    next_looper_ = next_looper_.Add(4);
    Looper looper;
    looper.owner = tid;
    looper.allow_non_callbacks = (opts & 1U) != 0;
    loopers_.emplace(handle.Value(), std::move(looper));
    thread_loopers_.emplace(tid, handle);
    return handle;
}

void AndroidModule::PollThreadLooper(std::uint64_t tid, const std::function<bool()>& interrupted) {
    static_cast<void>(PollAll({UINT32_MAX, 0, 0, 0}, tid, interrupted));
}

void AndroidModule::WakeThreadLooper(std::uint64_t tid) {
    std::scoped_lock lock(mutex_);
    if (const auto found = thread_loopers_.find(tid); found != thread_loopers_.end())
        RequireLooper(found->second).wake = true;
    ready_.notify_all();
}

void AndroidModule::NotifyLooperPollers() {
    // The Java deadline predicate is checked while holding this mutex. Pair
    // this notification with that check so a Clock step cannot be lost.
    std::scoped_lock lock(mutex_);
    ready_.notify_all();
}

void AndroidModule::RetireThreadLooper(std::uint64_t tid) {
    std::scoped_lock lock(mutex_);
    RetireThreadLooperLocked(tid);
}

void AndroidModule::ShutdownLoopers() {
    std::scoped_lock lock(mutex_);
    loopers_shutdown_ = true;
    RetireSensorQueues(std::nullopt);
    while (!thread_loopers_.empty())
        RetireThreadLooperLocked(thread_loopers_.begin()->first);
    ready_.notify_all();
}

std::uint32_t AndroidModule::PollAll(const std::array<std::uint32_t, 4>& args, std::uint64_t tid,
                                     const std::function<bool()>& interrupted, bool all) {
    const auto timeout = std::bit_cast<std::int32_t>(args[0]);
    for (std::size_t i = 1; i < args.size(); ++i)
        if (args[i] != 0)
            services_.address_space.Validate({memory::GuestAddress{args[i]}, 4}, memory::AccessType::write, tid);
    const auto started = looper_clock_.Ticks();
    const auto negative_result = [&](std::int32_t result) {
        for (std::size_t i = 1; i < args.size(); ++i)
            services_.Write32(args[i], 0, tid);
        return std::bit_cast<std::uint32_t>(result);
    };
    std::unique_lock lock(mutex_);
    for (;;) {
        SweepLoopers();
        const auto found = thread_loopers_.find(tid);
        if (loopers_shutdown_ || found == thread_loopers_.end()) return negative_result(-4);
        const auto handle = found->second;
        auto& looper = RequireLooper(handle);

        for (auto& [_, queue] : sensor_queues_)
            if (queue.looper == handle) UpdateSensorQueueSignal(queue);

        struct Ready {
            std::int32_t fd;
            std::uint32_t events;
            Looper::Request request;
        };

        std::vector<Ready> callbacks;
        std::optional<Ready> response;
        // Round-robin among ready descriptors; readiness is level-triggered and
        // comes from the real descriptor state, never a count of unrelated writes.
        auto request = looper.requests.upper_bound(looper.last_fd);
        for (std::size_t remaining = looper.requests.size(); remaining != 0; --remaining) {
            if (request == looper.requests.end()) request = looper.requests.begin();
            const auto [fd, item] = *request++;
            // ROM MessageQueue consumes managed readiness after reacquiring the VM lock.
            if (item.on_readable && !interrupted) continue;
            const auto available = looper_hooks_.poll_events(fd);
            const auto events = available ? (*available & (item.events | 28U)) : 16U;
            if (events == 0) continue;
            if (!item.callback.IsNull() || item.on_readable)
                callbacks.push_back({fd, events, item});
            else if (!response)
                response = Ready{fd, events, item};
        }
        const auto is_current = [&](const Ready& entry) {
            const auto state = loopers_.find(handle.Value());
            if (state == loopers_.end() || state->second.retired) return false;
            const auto current = state->second.requests.find(entry.fd);
            return current != state->second.requests.end() && current->second.generation == entry.request.generation;
        };
        bool dispatched = false;
        for (const auto& entry : callbacks) {
            if (!is_current(entry)) continue;
            lock.unlock();
            int keep = 1;
            if (entry.request.on_readable)
                entry.request.on_readable();
            else
                keep = looper_hooks_.invoke_callback(tid, entry.request.callback, entry.fd, entry.events,
                                                     entry.request.data);
            lock.lock();
            dispatched = true;
            if (!keep && is_current(entry)) RemoveLooperFdLocked(handle, entry.fd);
        }
        if (loopers_shutdown_ || !thread_loopers_.contains(tid)) return negative_result(-4);
        if (response && is_current(*response)) {
            services_.Write32(args[1], static_cast<std::uint32_t>(response->fd), tid);
            services_.Write32(args[2], response->events, tid);
            services_.Write32(args[3], response->request.data.Value(), tid);
            RequireLooper(handle).last_fd = response->fd;
            return static_cast<std::uint32_t>(response->request.ident);
        }
        if (dispatched && !all) return negative_result(-2);
        const InputQueueState* ready_input = nullptr;
        if (!managed_activity_seen_ && standalone_input_.looper == handle && !standalone_input_.pending.empty())
            ready_input = &standalone_input_;
        for (const auto& [_, resource] : resources_) {
            if (resource.input_active && resource.input_attached && resource.input.looper == handle &&
                !resource.input.pending.empty()) {
                ready_input = &resource.input;
                break;
            }
        }
        if (ready_input) {
            services_.Write32(args[1], 0, tid);
            services_.Write32(args[2], 1, tid);
            services_.Write32(args[3], ready_input->data, tid);
            return ready_input->ident;
        }
        if (RequireLooper(handle).wake) {
            RequireLooper(handle).wake = false;
            return negative_result(-1);
        }
        if (interrupted && interrupted()) return negative_result(-1);
        if (dispatched && timeout == 0) continue;
        const auto elapsed_ms = (looper_clock_.Ticks() - started) / 1000000U;
        if (timeout >= 0 && elapsed_ms >= static_cast<std::uint32_t>(timeout)) return negative_result(-3);
        // Polling also observes direct VFS operations and thread exit, which do
        // not necessarily enter a syscall observer. Clock owns all time readings.
        if (!dispatched) ready_.wait_for(lock, std::chrono::milliseconds(2));
    }
}

}  // namespace ogplay::runtime
