// Object monitors with owner, recursion and wait set (04 §4). The wait
// sequence follows AOSP vm/Sync.cpp waitMonitor step for step: check
// ownership, join the wait set, save and clear the recursion count, release
// the monitor, park, then re-acquire the monitor and restore the count
// before returning or throwing.

#include "ogplay/runtime/dexvm/vm_monitors.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm {
namespace {

// Parked threads re-evaluate their Clock deadline on this cadence. It is a
// scheduler wakeup, not a time source: every deadline decision below is
// made against the injected unified Clock.
constexpr auto kDeadlinePoll = std::chrono::milliseconds(2);

struct Monitor final {
    std::uint64_t owner{};
    std::int32_t recursion{};
    // Wait set in arrival order; notify() wakes the head (AOSP appends to
    // the tail and signals from the head).
    std::deque<std::uint64_t> wait_set;
    // Contexts blocked in monitor-enter (or re-acquiring after wait). Unlike
    // wait_set, these have an actual wait-for edge to owner.
    std::deque<std::uint64_t> entry_waiters;

    [[nodiscard]] bool CanAcquire(const std::uint64_t entrant) const {
        return recursion == 0 ? entry_waiters.empty() || entry_waiters.front() == entrant : owner == entrant;
    }
};

}  // namespace

class VmMonitorTable::Impl final {
public:
    Interpreter* vm{};
    VmMonotonicMillis now;
    VmMonotonicNanos now_nanos;
    VmClockAdvance advance;
    VmMonotonicMillis realtime;
    bool driver_blocked{};
    std::size_t driver_wait_depth{};
    std::int64_t stall_realtime{};
    std::int64_t stall_uptime{};
    mutable std::mutex mutex;
    mutable std::mutex clock_advance_mutex;
    std::condition_variable changed;
    std::unordered_map<std::uint32_t, std::unique_ptr<Monitor>> monitors;
    std::unordered_set<std::uint64_t> interrupted;
    // Contexts that have been woken out of a wait set and must not be
    // notified again until they re-enter one.
    std::unordered_set<std::uint64_t> woken;
    bool shutting_down{};

    [[nodiscard]] bool ClockDriverStalled() const noexcept { return driver_blocked || driver_wait_depth != 0; }

    // Called with clock_advance_mutex held on entry to a new stall.
    void BeginClockDriverStall() {
        if (!realtime) return;
        VmMonotonicMillis uptime;
        {
            const std::lock_guard guard(mutex);
            uptime = now;
        }
        stall_realtime = realtime();
        stall_uptime = uptime ? uptime() : 0;
    }

    [[nodiscard]] Monitor& MonitorFor(const VmObjectRef object) {
        auto& slot = monitors[object.Value()];
        if (!slot) slot = std::make_unique<Monitor>();
        return *slot;
    }

    [[nodiscard]] static VmJavaThrow NotOwner(const char* what) {
        return VmJavaThrow{"Ljava/lang/IllegalMonitorStateException;",
                           std::string("object is not locked by the calling "
                                       "thread before ") +
                               what};
    }

    void SetMonitorWaitState(const std::uint64_t owner, const VmThreadWaitState state) const {
        if (auto* threads = vm->AttachedThreadRuntime(); threads != nullptr) {
            threads->SetWaitState(owner, state);
        }
    }

    void RemoveEntryWaiter(Monitor& monitor, const std::uint64_t owner) {
        if (std::erase(monitor.entry_waiters, owner) != 0) changed.notify_all();
    }

    // Takes ownership of the monitor in entry order, parking behind earlier waiters.
    // The execution lock is released for the whole park.
    void AcquireLocked(std::unique_lock<std::mutex>& guard, const VmObjectRef object, const std::uint64_t owner,
                       const std::int32_t recursion) {
        auto* monitor = &MonitorFor(object);
        std::uint64_t signal_thread{};
        try {
            while (!shutting_down && !monitor->CanAcquire(owner)) {
                if (std::ranges::find(monitor->entry_waiters, owner) == monitor->entry_waiters.end())
                    monitor->entry_waiters.push_back(owner);
                guard.unlock();
                signal_thread = vm->NativeSignalThread(owner);
                SetMonitorWaitState(owner, VmThreadWaitState::monitor);
                auto& lock = vm->ExecutionLock();
                const auto depth = lock.ReleaseForBlocking();
                {
                    std::unique_lock parked(mutex);
                    const auto ready = [&] {
                        return shutting_down || MonitorFor(object).CanAcquire(owner) ||
                               vm->ExecutionDeadlineExpired(owner) || vm->NativeSignalDeliverable(signal_thread);
                    };
                    if (vm->HasExecutionDeadline(owner)) {
                        while (!ready())
                            changed.wait_for(parked, kDeadlinePoll);
                    } else
                        changed.wait(parked, ready);
                }
                lock.ReacquireAfterBlocking(depth);
                SetMonitorWaitState(owner, VmThreadWaitState::none);
                if (vm->NativeSignalDeliverable(signal_thread)) vm->DeliverNativeSignals(owner);
                guard.lock();
                monitor = &MonitorFor(object);
                vm->CheckExecutionDeadline(owner);
            }
        } catch (...) {
            if (!guard.owns_lock()) {
                SetMonitorWaitState(owner, VmThreadWaitState::none);
                guard.lock();
            }
            RemoveEntryWaiter(MonitorFor(object), owner);
            throw;
        }
        RemoveEntryWaiter(*monitor, owner);
        monitor->owner = owner;
        monitor->recursion = recursion;
    }
};

VmMonitorTable::VmMonitorTable(Interpreter& vm) : impl_(std::make_unique<Impl>()) {
    impl_->vm = &vm;
}

VmMonitorTable::~VmMonitorTable() = default;

void VmMonitorTable::SetTimeSource(VmMonotonicMillis source) {
    const std::lock_guard guard(impl_->mutex);
    impl_->now = source;
    impl_->now_nanos = source ? VmMonotonicNanos{[source = std::move(source)] {
        const auto millis = source();
        constexpr std::int64_t scale = 1'000'000;
        if (millis > std::numeric_limits<std::int64_t>::max() / scale ||
            millis < std::numeric_limits<std::int64_t>::min() / scale)
            throw DexVmError(DexVmErrorReason::internal_invariant, "monotonic Clock exceeds nanosecond range");
        return millis * scale;
    }}
                              : VmMonotonicNanos{};
}

void VmMonitorTable::SetNanoTimeSource(VmMonotonicNanos source) {
    const std::lock_guard guard(impl_->mutex);
    impl_->now_nanos = source;
    impl_->now =
        source ? VmMonotonicMillis{[source = std::move(source)] { return source() / 1'000'000; }} : VmMonotonicMillis{};
}

VmMonotonicNanos VmMonitorTable::NanoTimeSource() const {
    const std::lock_guard guard(impl_->mutex);
    return impl_->now_nanos;
}

VmMonotonicMillis VmMonitorTable::TimeSource() const {
    const std::lock_guard guard(impl_->mutex);
    return impl_->now;
}

void VmMonitorTable::SetClockAdvance(VmClockAdvance advance) {
    const std::lock_guard guard(impl_->mutex);
    impl_->advance = std::move(advance);
}

void VmMonitorTable::SetRealtimeSource(VmMonotonicMillis source) {
    const std::lock_guard guard(impl_->clock_advance_mutex);
    impl_->realtime = std::move(source);
}

void VmMonitorTable::ObserveClockDriverBlocking(const bool blocked) {
    const std::lock_guard guard(impl_->clock_advance_mutex);
    if (blocked && !impl_->ClockDriverStalled()) impl_->BeginClockDriverStall();
    impl_->driver_blocked = blocked;
}

void VmMonitorTable::ChangeClockDriverWait(const bool entered) {
    const std::lock_guard guard(impl_->clock_advance_mutex);
    if (entered) {
        if (!impl_->ClockDriverStalled()) impl_->BeginClockDriverStall();
        ++impl_->driver_wait_depth;
    } else {
        --impl_->driver_wait_depth;
    }
}

VmClockDriverWaitScope::VmClockDriverWaitScope(VmMonitorTable* const monitors) : monitors_(monitors) {
    if (monitors_) monitors_->ChangeClockDriverWait(true);
}

VmClockDriverWaitScope::~VmClockDriverWaitScope() {
    if (monitors_) monitors_->ChangeClockDriverWait(false);
}

bool VmMonitorTable::TryAdvanceClockForTimedPark(const std::uint64_t, const std::int64_t deadline_millis) const {
    const std::lock_guard advance_guard(impl_->clock_advance_mutex);
    VmClockAdvance advance;
    VmMonotonicMillis now;
    {
        const std::lock_guard guard(impl_->mutex);
        advance = impl_->advance;
        now = impl_->now;
    }
    if (!now) return false;
    const auto current = now();
    if (current >= deadline_millis) return true;
    if (!advance || !impl_->realtime || !impl_->ClockDriverStalled()) return false;
    const auto elapsed = std::max<std::int64_t>(0, impl_->realtime() - impl_->stall_realtime);
    const auto allowed = std::min(deadline_millis, impl_->stall_uptime + elapsed);
    if (allowed > current) advance(allowed - current);
    return now() >= deadline_millis;
}

void VmMonitorTable::Enter(const VmObjectRef object, const std::uint64_t owner) {
    std::unique_lock guard(impl_->mutex);
    auto& monitor = impl_->MonitorFor(object);
    if (monitor.recursion != 0 && monitor.owner == owner) {
        ++monitor.recursion;
        return;
    }
    if (monitor.recursion == 0 && monitor.entry_waiters.empty()) {
        monitor.owner = owner;
        monitor.recursion = 1;
        return;
    }
    impl_->AcquireLocked(guard, object, owner, 1);
}

void VmMonitorTable::Exit(const VmObjectRef object, const std::uint64_t owner) {
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        if (monitor.recursion == 0 || monitor.owner != owner) {
            throw Impl::NotOwner("monitor-exit");
        }
        if (--monitor.recursion > 0) return;
        monitor.owner = 0;
    }
    impl_->changed.notify_all();
}

bool VmMonitorTable::IsOwner(const VmObjectRef object, const std::uint64_t owner) const {
    const std::lock_guard guard(impl_->mutex);
    const auto found = impl_->monitors.find(object.Value());
    if (found == impl_->monitors.end()) return false;
    return found->second->recursion > 0 && found->second->owner == owner;
}

std::size_t VmMonitorTable::HeldCount(const std::uint64_t owner) const {
    const std::lock_guard guard(impl_->mutex);
    std::size_t held = 0;
    for (const auto& [_, monitor] : impl_->monitors) {
        if (monitor->recursion > 0 && monitor->owner == owner) ++held;
    }
    return held;
}

std::size_t VmMonitorTable::WaitingCount(const VmObjectRef object) const {
    const std::lock_guard guard(impl_->mutex);
    const auto found = impl_->monitors.find(object.Value());
    if (found == impl_->monitors.end()) return 0;
    return found->second->wait_set.size();
}

void VmMonitorTable::ReleaseObjectForGc(const VmObjectRef object) {
    const std::lock_guard guard(impl_->mutex);
    const auto found = impl_->monitors.find(object.Value());
    if (found == impl_->monitors.end()) return;
    const auto& monitor = *found->second;
    if (monitor.owner != 0 || monitor.recursion != 0 || !monitor.wait_set.empty() || !monitor.entry_waiters.empty()) {
        throw DexVmError(DexVmErrorReason::internal_invariant, "GC reached an owned or waited-on object monitor");
    }
    impl_->monitors.erase(found);
}

VmWaitOutcome VmMonitorTable::Wait(const VmObjectRef object, const std::uint64_t owner,
                                   const std::int64_t timeout_millis) {
    if (timeout_millis < 0) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "timeout arguments out of range"};
    }
    const bool timed = timeout_millis > 0;
    VmMonotonicMillis clock;
    std::int32_t saved_recursion{};
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        if (monitor.recursion == 0 || monitor.owner != owner) {
            throw Impl::NotOwner("wait()");
        }
        clock = impl_->now;
        // The interrupt flag is honoured before we ever park, exactly as
        // AOSP checks self->interrupted under waitMutex.
        if (impl_->interrupted.erase(owner) != 0) {
            return VmWaitOutcome::interrupted;
        }
        if (impl_->shutting_down) return VmWaitOutcome::shut_down;
        saved_recursion = monitor.recursion;
    }
    if (timed && !clock) {
        if (auto* ledger = impl_->vm->Ledger(); ledger != nullptr) {
            ledger->RecordUnimplemented("dexvm.monitors.wait_without_clock", 0);
        }
        throw DexVmError(DexVmErrorReason::internal_invariant, "a timed Object.wait needs the unified Clock; this "
                                                               "session published no time source");
    }

    // Join the wait set and release the monitor completely, keeping the
    // AOSP ordering: append first, then clear count and owner.
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        monitor.wait_set.push_back(owner);
        monitor.recursion = 0;
        monitor.owner = 0;
        impl_->woken.erase(owner);
    }
    impl_->changed.notify_all();

    const std::int64_t deadline = timed ? clock() + timeout_millis : 0;
    auto outcome = VmWaitOutcome::notified;
    const auto signal_thread = impl_->vm->NativeSignalThread(owner);
    impl_->SetMonitorWaitState(owner, VmThreadWaitState::monitor);
    auto& lock = impl_->vm->ExecutionLock();
    auto depth = lock.ReleaseForBlocking();
    bool deadline_expired = false;
    {
        std::unique_lock parked(impl_->mutex);
        while (true) {
            if (impl_->vm->ExecutionDeadlineExpired(owner)) {
                deadline_expired = true;
                break;
            }
            if (impl_->shutting_down) {
                outcome = VmWaitOutcome::shut_down;
                break;
            }
            if (impl_->interrupted.count(owner) != 0) {
                outcome = VmWaitOutcome::interrupted;
                break;
            }
            if (impl_->woken.erase(owner) != 0) {
                outcome = VmWaitOutcome::notified;
                break;
            }
            if (timed && clock() >= deadline) {
                outcome = VmWaitOutcome::timed_out;
                break;
            }
            if (impl_->vm->NativeSignalDeliverable(signal_thread)) {
                parked.unlock();
                lock.ReacquireAfterBlocking(depth);
                try {
                    impl_->vm->DeliverNativeSignals(owner);
                } catch (...) {
                    {
                        const std::lock_guard guard(impl_->mutex);
                        std::erase(impl_->MonitorFor(object).wait_set, owner);
                        impl_->woken.erase(owner);
                    }
                    impl_->SetMonitorWaitState(owner, VmThreadWaitState::none);
                    throw;
                }
                depth = lock.ReleaseForBlocking();
                parked.lock();
                continue;
            }
            if (timed || impl_->vm->HasExecutionDeadline(owner)) {
                // Deadline decisions stay with the unified Clock; this only
                // schedules the next re-check.
                impl_->changed.wait_for(parked, kDeadlinePoll);
                // Recheck the real-time allowance on every poll.
                if (timed) {
                    parked.unlock();
                    static_cast<void>(TryAdvanceClockForTimedPark(owner, deadline));
                    parked.lock();
                }
            } else {
                impl_->changed.wait(parked);
            }
        }
    }
    lock.ReacquireAfterBlocking(depth);

    if (deadline_expired || impl_->vm->ExecutionDeadlineExpired(owner)) {
        // Internal scope failure abandons guest execution, rather than returning
        // an InterruptedException the application could swallow and retry.
        {
            const std::lock_guard guard(impl_->mutex);
            std::erase(impl_->MonitorFor(object).wait_set, owner);
            impl_->woken.erase(owner);
        }
        impl_->SetMonitorWaitState(owner, VmThreadWaitState::none);
        impl_->vm->CheckExecutionDeadline(owner);
    }

    // Re-acquire the monitor and restore the recursion depth before this
    // call returns or its caller throws (JLS ordering, AOSP "done:" label).
    {
        std::unique_lock guard(impl_->mutex);
        try {
            impl_->AcquireLocked(guard, object, owner, saved_recursion);
        } catch (...) {
            std::erase(impl_->MonitorFor(object).wait_set, owner);
            impl_->woken.erase(owner);
            guard.unlock();
            impl_->SetMonitorWaitState(owner, VmThreadWaitState::none);
            throw;
        }
        auto& monitor = impl_->MonitorFor(object);
        const auto position = std::find(monitor.wait_set.begin(), monitor.wait_set.end(), owner);
        if (position != monitor.wait_set.end()) {
            monitor.wait_set.erase(position);
        }
        if (outcome == VmWaitOutcome::interrupted) {
            // "The interrupted status of the current thread is cleared when
            // this exception is thrown."
            impl_->interrupted.erase(owner);
        }
    }
    impl_->SetMonitorWaitState(owner, VmThreadWaitState::none);
    return outcome;
}

void VmMonitorTable::Notify(const VmObjectRef object, const std::uint64_t owner) {
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        if (monitor.recursion == 0 || monitor.owner != owner) {
            throw Impl::NotOwner("notify()");
        }
        if (monitor.wait_set.empty()) return;
        const auto waiter = monitor.wait_set.front();
        monitor.wait_set.pop_front();
        impl_->woken.insert(waiter);
    }
    impl_->changed.notify_all();
}

void VmMonitorTable::NotifyAll(const VmObjectRef object, const std::uint64_t owner) {
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        if (monitor.recursion == 0 || monitor.owner != owner) {
            throw Impl::NotOwner("notifyAll()");
        }
        for (const auto waiter : monitor.wait_set) {
            impl_->woken.insert(waiter);
        }
        monitor.wait_set.clear();
    }
    impl_->changed.notify_all();
}

void VmMonitorTable::NotifyForGc(const VmObjectRef object) {
    {
        const std::lock_guard guard(impl_->mutex);
        auto& monitor = impl_->MonitorFor(object);
        for (const auto waiter : monitor.wait_set)
            impl_->woken.insert(waiter);
        monitor.wait_set.clear();
    }
    impl_->changed.notify_all();
}

void VmMonitorTable::WakeParked() {
    {
        const std::lock_guard guard(impl_->mutex);
    }
    impl_->changed.notify_all();
}

void VmMonitorTable::Interrupt(const std::uint64_t owner) {
    {
        const std::lock_guard guard(impl_->mutex);
        impl_->interrupted.insert(owner);
    }
    impl_->changed.notify_all();
}

bool VmMonitorTable::Interrupted(const std::uint64_t owner) const {
    const std::lock_guard guard(impl_->mutex);
    return impl_->interrupted.count(owner) != 0;
}

bool VmMonitorTable::ClearInterrupt(const std::uint64_t owner) {
    const std::lock_guard guard(impl_->mutex);
    return impl_->interrupted.erase(owner) != 0;
}

void VmMonitorTable::ReleaseAll(const std::uint64_t owner) {
    {
        const std::lock_guard guard(impl_->mutex);
        for (auto& [_, monitor] : impl_->monitors) {
            if (monitor->recursion > 0 && monitor->owner == owner) {
                monitor->recursion = 0;
                monitor->owner = 0;
            }
            const auto position = std::find(monitor->wait_set.begin(), monitor->wait_set.end(), owner);
            if (position != monitor->wait_set.end()) {
                monitor->wait_set.erase(position);
            }
            const auto entrant = std::find(monitor->entry_waiters.begin(), monitor->entry_waiters.end(), owner);
            if (entrant != monitor->entry_waiters.end()) {
                monitor->entry_waiters.erase(entrant);
            }
        }
        impl_->woken.erase(owner);
        impl_->interrupted.erase(owner);
    }
    impl_->changed.notify_all();
}

void VmMonitorTable::Shutdown() {
    {
        const std::lock_guard guard(impl_->mutex);
        impl_->shutting_down = true;
    }
    impl_->changed.notify_all();
}

bool VmMonitorTable::ShuttingDown() const {
    const std::lock_guard guard(impl_->mutex);
    return impl_->shutting_down;
}

VmMonitorSnapshot VmMonitorTable::Snapshot(const VmObjectRef object) const {
    if (!object.IsValid()) return {};
    const std::lock_guard guard(impl_->mutex);
    const auto found = impl_->monitors.find(object.Value());
    if (found == impl_->monitors.end()) {
        return {.shutting_down = impl_->shutting_down};
    }
    return {found->second->owner, static_cast<std::size_t>(found->second->recursion), found->second->wait_set.size(),
            impl_->shutting_down};
}

std::optional<bool> VmMonitorTable::TryInterrupted(const std::uint64_t owner) const {
    std::unique_lock guard(impl_->mutex, std::try_to_lock);
    if (!guard.owns_lock()) return std::nullopt;
    return impl_->interrupted.contains(owner);
}

std::optional<std::vector<VmMonitorDiagnosticSnapshot>> VmMonitorTable::TrySnapshotAll() const {
    std::unique_lock guard(impl_->mutex, std::try_to_lock);
    if (!guard.owns_lock()) return std::nullopt;
    std::vector<VmMonitorDiagnosticSnapshot> result;
    result.reserve(impl_->monitors.size());
    for (const auto& [object, monitor] : impl_->monitors) {
        if (monitor->owner == 0U && monitor->recursion == 0 && monitor->entry_waiters.empty() &&
            monitor->wait_set.empty()) {
            continue;
        }
        result.push_back({object,
                          monitor->owner,
                          static_cast<std::uint32_t>(monitor->recursion),
                          {monitor->entry_waiters.begin(), monitor->entry_waiters.end()},
                          {monitor->wait_set.begin(), monitor->wait_set.end()}});
    }
    std::ranges::sort(result, {}, &VmMonitorDiagnosticSnapshot::object);
    return result;
}

}  // namespace ogplay::runtime::dexvm
