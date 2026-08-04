#include "ogplay/runtime/guest_thread_lifecycle.h"

#include <map>
#include <mutex>
#include <utility>

namespace ogplay::runtime {

class GuestThreadLifecycle::Impl final {
public:
    GuestThreadRuntimeState& Require(const std::uint64_t thread_id) {
        const auto found = states.find(thread_id);
        if (found == states.end()) {
            throw GuestThreadLifecycleError("unknown guest thread");
        }
        return found->second;
    }

    const GuestThreadRuntimeState& Require(
        const std::uint64_t thread_id) const {
        const auto found = states.find(thread_id);
        if (found == states.end()) {
            throw GuestThreadLifecycleError("unknown guest thread");
        }
        return found->second;
    }

    mutable std::mutex mutex;
    std::map<std::uint64_t, GuestThreadRuntimeState> states;
};

GuestThreadLifecycle::GuestThreadLifecycle()
    : impl_(std::make_unique<Impl>()) {}
GuestThreadLifecycle::~GuestThreadLifecycle() = default;
GuestThreadLifecycle::GuestThreadLifecycle(GuestThreadLifecycle&&) noexcept =
    default;
GuestThreadLifecycle& GuestThreadLifecycle::operator=(
    GuestThreadLifecycle&&) noexcept = default;

void GuestThreadLifecycle::Register(
    const std::uint64_t thread_id,
    const memory::GuestAddress thread_pointer) {
    if (thread_id == 0) {
        throw GuestThreadLifecycleError("guest thread id must be non-zero");
    }
    std::scoped_lock lock(impl_->mutex);
    const auto [position, inserted] = impl_->states.emplace(
        thread_id, GuestThreadRuntimeState{thread_id, thread_pointer});
    static_cast<void>(position);
    if (!inserted) {
        throw GuestThreadLifecycleError("guest thread is already registered");
    }
}

void GuestThreadLifecycle::SetThreadPointer(
    const std::uint64_t thread_id,
    const memory::GuestAddress thread_pointer) {
    std::scoped_lock lock(impl_->mutex);
    auto& state = impl_->Require(thread_id);
    if (state.status != GuestThreadStatus::running) {
        throw GuestThreadLifecycleError(
            "cannot update a non-running guest thread");
    }
    state.thread_pointer = thread_pointer;
}

void GuestThreadLifecycle::SetClearChildTid(
    const std::uint64_t thread_id, const memory::GuestAddress address) {
    std::scoped_lock lock(impl_->mutex);
    auto& state = impl_->Require(thread_id);
    if (state.status != GuestThreadStatus::running) {
        throw GuestThreadLifecycleError(
            "cannot update a non-running guest thread");
    }
    state.clear_child_tid = address;
}

void GuestThreadLifecycle::RequestExit(const std::uint64_t thread_id,
                                       const std::int32_t exit_code) {
    std::scoped_lock lock(impl_->mutex);
    auto& state = impl_->Require(thread_id);
    if (state.status == GuestThreadStatus::exited) {
        throw GuestThreadLifecycleError("guest thread has already exited");
    }
    state.exit_code = exit_code;
    state.status = GuestThreadStatus::exit_requested;
}

void GuestThreadLifecycle::RequestExitGroup(
    const std::uint64_t requesting_thread_id,
    const std::int32_t exit_code) {
    std::scoped_lock lock(impl_->mutex);
    const auto& requester = impl_->Require(requesting_thread_id);
    if (requester.status == GuestThreadStatus::exited) {
        throw GuestThreadLifecycleError("requesting guest thread has exited");
    }
    for (auto& [thread_id, state] : impl_->states) {
        static_cast<void>(thread_id);
        if (state.status == GuestThreadStatus::exited) continue;
        state.exit_code = exit_code;
        state.status = GuestThreadStatus::exit_requested;
    }
}

GuestThreadRuntimeState GuestThreadLifecycle::CompleteExit(
    const std::uint64_t thread_id) {
    std::scoped_lock lock(impl_->mutex);
    auto& state = impl_->Require(thread_id);
    if (state.status != GuestThreadStatus::exit_requested) {
        throw GuestThreadLifecycleError(
            "guest thread exit was not requested");
    }
    state.status = GuestThreadStatus::exited;
    return state;
}

GuestThreadRuntimeState GuestThreadLifecycle::Reap(
    const std::uint64_t thread_id) {
    std::scoped_lock lock(impl_->mutex);
    const auto state = impl_->Require(thread_id);
    if (state.status != GuestThreadStatus::exited) {
        throw GuestThreadLifecycleError("guest thread is not exited");
    }
    impl_->states.erase(thread_id);
    return state;
}

GuestThreadRuntimeState GuestThreadLifecycle::State(
    const std::uint64_t thread_id) const {
    std::scoped_lock lock(impl_->mutex);
    return impl_->Require(thread_id);
}

std::vector<GuestThreadRuntimeState> GuestThreadLifecycle::States() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<GuestThreadRuntimeState> result;
    result.reserve(impl_->states.size());
    for (const auto& [thread_id, state] : impl_->states) {
        static_cast<void>(thread_id);
        result.push_back(state);
    }
    return result;
}

}  // namespace ogplay::runtime
