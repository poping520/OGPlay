#include "runtime/boundary/modules/android/android_module.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <stdexcept>

#include "ogplay/hal/clock.h"

namespace ogplay::runtime {

AndroidModule::Looper& AndroidModule::RequireLooper(memory::GuestAddress handle) {
    const auto found = loopers_.find(handle.Value());
    if (found == loopers_.end()) throw std::invalid_argument("ALooper is stale or unregistered");
    return found->second;
}

void AndroidModule::RetireThreadLooperLocked(std::uint64_t tid) {
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
    if (const auto found = thread_loopers_.find(tid); found != thread_loopers_.end())
        return found->second;
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

void AndroidModule::RetireThreadLooper(std::uint64_t tid) {
    std::scoped_lock lock(mutex_);
    RetireThreadLooperLocked(tid);
}

void AndroidModule::ShutdownLoopers() {
    std::scoped_lock lock(mutex_);
    loopers_shutdown_ = true;
    while (!thread_loopers_.empty()) RetireThreadLooperLocked(thread_loopers_.begin()->first);
    ready_.notify_all();
}

std::uint32_t AndroidModule::PollAll(const std::array<std::uint32_t, 4>& args,
                                    std::uint64_t tid) {
    const auto timeout = std::bit_cast<std::int32_t>(args[0]);
    for (std::size_t i = 1; i < args.size(); ++i)
        if (args[i] != 0) services_.address_space.Validate(
            {memory::GuestAddress{args[i]}, 4}, memory::AccessType::write, tid);
    const auto started = looper_clock_.Ticks();
    const auto negative_result = [&](std::int32_t result) {
        for (std::size_t i = 1; i < args.size(); ++i) services_.Write32(args[i], 0, tid);
        return std::bit_cast<std::uint32_t>(result);
    };
    std::unique_lock lock(mutex_);
    for (;;) {
        SweepLoopers();
        const auto found = thread_loopers_.find(tid);
        if (loopers_shutdown_ || found == thread_loopers_.end()) return negative_result(-4);
        auto& looper = RequireLooper(found->second);
        // Round-robin among ready descriptors; readiness is level-triggered and
        // comes from the real descriptor state, never a count of unrelated writes.
        auto request = looper.requests.upper_bound(looper.last_fd);
        for (std::size_t remaining = looper.requests.size(); remaining != 0; --remaining) {
            if (request == looper.requests.end()) request = looper.requests.begin();
            const auto [fd, item] = *request++;
            const auto available = looper_hooks_.poll_events(fd);
            const auto events = available ? (*available & (item.events | 28U)) : 16U;
            if (events == 0) continue;
            // Validate/write before consuming the ready result.
            services_.Write32(args[1], static_cast<std::uint32_t>(fd), tid);
            services_.Write32(args[2], events, tid);
            services_.Write32(args[3], item.data.Value(), tid);
            looper.last_fd = fd;
            return static_cast<std::uint32_t>(item.ident);
        }
        const InputQueueState* ready_input = nullptr;
        if (!managed_activity_seen_ && standalone_input_.looper == found->second &&
            !standalone_input_.pending.empty()) ready_input = &standalone_input_;
        for (const auto& [_, resource] : resources_) {
            if (resource.input_active && resource.input_attached &&
                resource.input.looper == found->second && !resource.input.pending.empty()) {
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
        if (looper.wake) {
            looper.wake = false;
            return negative_result(-1);
        }
        const auto elapsed_ms = (looper_clock_.Ticks() - started) / 1000000U;
        if (timeout >= 0 && elapsed_ms >= static_cast<std::uint32_t>(timeout))
            return negative_result(-3);
        // Polling also observes direct VFS operations and thread exit, which do
        // not necessarily enter a syscall observer. Clock owns all time readings.
        ready_.wait_for(lock, std::chrono::milliseconds(2));
    }
}

}  // namespace ogplay::runtime
