#include "ogplay/runtime/dexvm/interpreter.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "interpreter_internal.h"
#include "ogplay/runtime/dexvm/vm_threads.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::runtime::dexvm {

namespace {

thread_local std::unordered_map<const void*, InterpreterExecutionState*>
    active_executions;

}  // namespace

// ---- VmExecutionLock -------------------------------------------------------

class VmExecutionLock::Impl final {
public:
    std::mutex mutex;
    std::condition_variable released;
    std::thread::id owner;
    std::size_t depth{};
    std::atomic<void*> blocking_observer_context{};
    std::atomic<BlockingObserver> blocking_observer{};
};

VmExecutionLock::VmExecutionLock() : impl_(std::make_unique<Impl>()) {}
VmExecutionLock::~VmExecutionLock() = default;

void VmExecutionLock::Acquire() {
    std::unique_lock lock(impl_->mutex);
    const auto self = std::this_thread::get_id();
    if (impl_->depth > 0 && impl_->owner == self) {
        ++impl_->depth;
        return;
    }
    impl_->released.wait(lock, [this] { return impl_->depth == 0; });
    impl_->owner = self;
    impl_->depth = 1;
}

void VmExecutionLock::Release() {
    std::unique_lock lock(impl_->mutex);
    if (impl_->depth == 0 || impl_->owner != std::this_thread::get_id()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "releasing a DexVM execution lock that is not held "
                         "by the calling thread");
    }
    if (--impl_->depth > 0) return;
    impl_->owner = std::thread::id{};
    lock.unlock();
    impl_->released.notify_all();
}

std::size_t VmExecutionLock::ReleaseForBlocking() {
    const auto self = std::this_thread::get_id();
    std::unique_lock lock(impl_->mutex);
    if (impl_->depth == 0 || impl_->owner != self) {
        return 0;
    }
    const auto held = impl_->depth;
    impl_->depth = 0;
    impl_->owner = std::thread::id{};
    lock.unlock();
    impl_->released.notify_all();
    const auto observer =
        impl_->blocking_observer.load(std::memory_order_acquire);
    if (observer != nullptr) {
        observer(impl_->blocking_observer_context.load(
                     std::memory_order_relaxed),
                 self, true);
    }
    return held;
}

void VmExecutionLock::ReacquireAfterBlocking(const std::size_t depth) {
    if (depth == 0) return;
    const auto observer =
        impl_->blocking_observer.load(std::memory_order_acquire);
    if (observer != nullptr) {
        observer(impl_->blocking_observer_context.load(
                     std::memory_order_relaxed),
                 std::this_thread::get_id(), false);
    }
    // The thread is no longer blocked once it starts competing for the VM
    // lock. Notify first so a conditional pacer makes the current owner park
    // instead of repeatedly bypassing while this thread waits to reacquire.
    Acquire();
    std::lock_guard lock(impl_->mutex);
    impl_->depth = depth;
}

bool VmExecutionLock::HeldByCurrentThread() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->depth > 0 && impl_->owner == std::this_thread::get_id();
}

void VmExecutionLock::SetBlockingObserver(
    void* context, const BlockingObserver observer) noexcept {
    impl_->blocking_observer_context.store(context, std::memory_order_relaxed);
    impl_->blocking_observer.store(observer, std::memory_order_release);
}

InterpreterExecutionScope::InterpreterExecutionScope(
    void* interpreter, InterpreterExecutionState& execution)
    : interpreter_(interpreter) {
    const auto found = active_executions.find(interpreter_);
    previous_ = found == active_executions.end() ? nullptr : found->second;
    if (previous_ != nullptr && previous_ != &execution) {
        throw DexVmError(
            DexVmErrorReason::internal_invariant,
            "cannot switch DexVM execution context inside an active call");
    }
    active_executions[interpreter_] = &execution;
}

InterpreterExecutionScope::~InterpreterExecutionScope() {
    if (previous_ == nullptr) {
        active_executions.erase(interpreter_);
    } else {
        active_executions[interpreter_] = previous_;
    }
}

InterpreterExecutionState& Interpreter::Impl::Execution() {
    const auto active = active_executions.find(this);
    if (active != active_executions.end()) return *active->second;
    return *default_execution;
}

const InterpreterExecutionState& Interpreter::Impl::Execution() const {
    const auto active = active_executions.find(this);
    if (active != active_executions.end()) return *active->second;
    return *default_execution;
}

InterpreterExecutionState& Interpreter::Impl::Execution(
    const InterpreterExecutionContext& context) {
    if (!context.BelongsTo(owner)) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "execution context belongs to another interpreter");
    }
    const std::lock_guard lock(executions_mutex);
    const auto found = executions.find(context.Token());
    if (found == executions.end()) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "execution context is not registered");
    }
    return *found->second;
}

const InterpreterExecutionState& Interpreter::Impl::Execution(
    const InterpreterExecutionContext& context) const {
    return const_cast<Impl*>(this)->Execution(context);
}

InterpreterExecutionContext Interpreter::CreateExecutionContext() {
    InterpreterExecutionContext context;
    context.owner_ = this;
    const std::lock_guard lock(impl_->executions_mutex);
    context.token_ = impl_->next_execution_token++;
    auto execution = std::make_unique<InterpreterExecutionState>();
    execution->token = context.token_;
    impl_->executions.emplace(context.token_, std::move(execution));
    return context;
}

void Interpreter::DiscardExecutionContext(
    const InterpreterExecutionContext& context) {
    const auto& execution = impl_->Execution(context);
    if (!execution.frames.empty()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "cannot discard an execution context with live "
                         "interpreted frames");
    }
    const std::lock_guard lock(impl_->executions_mutex);
    impl_->executions.erase(context.Token());
}

InterpreterExecutionSnapshot Interpreter::ExecutionSnapshot(
    const InterpreterExecutionContext& context) const {
    const auto& execution = impl_->Execution(context);
    InterpreterExecutionSnapshot snapshot;
    snapshot.frame_depth = execution.frames.size();
    snapshot.has_pending_exception = execution.pending_exception.IsValid();
    snapshot.ticks = execution.ticks;
    snapshot.held_monitor_count = impl_->monitors->HeldCount(execution.token);
    snapshot.native_depth = execution.native_depth;
    snapshot.stop_requested =
        execution.stop_requested.load(std::memory_order_relaxed);
    return snapshot;
}

void Interpreter::RequestStop(const InterpreterExecutionContext& context) {
    impl_->Execution(context).stop_requested.store(true,
                                                   std::memory_order_relaxed);
    impl_->clinit_changed.notify_all();
}

void Interpreter::UnwindStoppedExecutionContext(
    const InterpreterExecutionContext& context) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution(context);
    if (!execution.stop_requested.load(std::memory_order_relaxed)) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "cannot unwind an execution context that was not "
                         "stopped");
    }
    if (execution.native_depth != 0U) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "stopped execution context still has live native "
                         "frames after its host thread joined");
    }
    while (!execution.frames.empty()) {
        impl_->ReleaseFrameMonitor(execution.frames.back());
        execution.frames.pop_back();
    }
    execution.pending_exception = VmObjectRef{};
    execution.pending_exception_class = DexClassId{};
    execution.exit_result = VmValue::Void();
}

VmExecutionLock& Interpreter::ExecutionLock() noexcept {
    return impl_->execution_lock;
}

void Interpreter::SetGcIntegration(InterpreterGcIntegration integration) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    impl_->gc_integration = std::move(integration);
}

void Interpreter::SetThreadRuntime(VmThreadRuntime* threads) noexcept {
    impl_->threads = threads;
}

VmThreadRuntime& Interpreter::Threads() {
    if (impl_->threads == nullptr) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "java.lang.Thread runtime is not attached");
    }
    return *impl_->threads;
}

void Interpreter::VisitRoots(const VmRootVisitor& visitor) {
    if (!visitor) return;
    const std::lock_guard contexts_lock(impl_->executions_mutex);
    for (const auto& [_, execution] : impl_->executions) {
        for (const auto& frame : execution->frames) {
            for (const auto& slot : frame.regs) {
                if (slot.tag == SlotTag::ref) visitor(VmObjectRef(slot.bits));
            }
            if (frame.last_result.kind == VmValue::Kind::ref) {
                visitor(frame.last_result.ref);
            }
            visitor(frame.caught);
        }
        visitor(execution->pending_exception);
        if (execution->exit_result.kind == VmValue::Kind::ref) {
            visitor(execution->exit_result.ref);
        }
    }
    for (const auto class_id : impl_->linker->AllClasses()) {
        const auto& linked = impl_->linker->Class(class_id);
        for (const auto slot : linked.static_ref_slots) {
            visitor(VmObjectRef(linked.static_storage[slot]));
        }
    }
    impl_->model->VisitPermanentRoots(visitor);
    impl_->class_loaders->VisitRoots(visitor);
    if (impl_->gc_integration.visit_jni_roots) {
        impl_->gc_integration.visit_jni_roots(
            [&](const JniObjectIdentity identity) {
                visitor(impl_->model->FindIdentity(identity));
            });
    }
    if (impl_->threads != nullptr) impl_->threads->VisitThreadRoots(visitor);
    if (impl_->gc_integration.visit_session_roots) {
        impl_->gc_integration.visit_session_roots(visitor);
    }
}

std::size_t Interpreter::RegisteredIntrinsicSideTableCount() const noexcept {
    return impl_->intrinsic_state_tables.size();
}

void Interpreter::RegisterIntrinsicStateTable(IntrinsicStateTableHooks hooks) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    if (hooks.name.empty() || !hooks.sweep) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "intrinsic state table needs a name and sweep hook");
    }
    const auto duplicate = std::find_if(
        impl_->intrinsic_state_tables.begin(),
        impl_->intrinsic_state_tables.end(), [&](const auto& registered) {
            return registered.name == hooks.name;
        });
    if (duplicate != impl_->intrinsic_state_tables.end()) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "intrinsic state table is already registered: " +
                             hooks.name);
    }
    impl_->intrinsic_state_tables.push_back(std::move(hooks));
}

void Interpreter::Impl::TraceIntrinsicSideTables(
    const VmObjectRef owner_ref, const VmRootVisitor& visitor) const {
    for (const auto& table : intrinsic_state_tables) {
        if (table.trace) table.trace(owner_ref, visitor);
    }
}

GcMarkResult Interpreter::MarkReachable() {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    std::vector<VmObjectRef> roots;
    VisitRoots([&](const VmObjectRef ref) {
        if (ref.IsValid()) roots.push_back(ref);
    });
    return impl_->model->MarkReachable(
        roots, [this](const VmObjectRef owner, const VmRootVisitor& visitor) {
            impl_->TraceIntrinsicSideTables(owner, visitor);
        });
}

GcSweepResult Interpreter::SweepGarbage(const GcMarkResult& mark) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    return impl_->model->Sweep(
        mark,
        GcSweepHooks{
            [this](const VmObjectRef ref, const VmObjectKind kind,
                   const DexClassId java_class,
                   const std::uint64_t host_state) -> bool {
                bool destructor_ran = false;
                if (kind == VmObjectKind::host_backed &&
                    java_class.IsValid()) {
                    const auto& linked = impl_->linker->Class(java_class);
                    if (linked.host_state_destructor) {
                        linked.host_state_destructor(host_state);
                        destructor_ran = true;
                    }
                }
                for (const auto& table : impl_->intrinsic_state_tables) {
                    table.sweep(ref);
                }
                impl_->monitors->ReleaseObjectForGc(ref);
                return destructor_ran;
            },
            [this](VmObjectRef, const JniObjectIdentity identity) {
                if (impl_->gc_integration.clear_weak_references) {
                    impl_->gc_integration.clear_weak_references(identity);
                }
            }});
}

GcSweepResult Interpreter::CollectGarbage(const std::string_view trigger) {
    VmExecutionLockScope lock_scope(impl_->execution_lock);
    auto& execution = impl_->Execution();
    impl_->RecordTrace(DexVmTraceKind::gc_begin, execution);
    const auto mark = MarkReachable();
    const auto swept = SweepGarbage(mark);
    impl_->RecordTrace(DexVmTraceKind::gc_end, execution, nullptr, 0, 0,
                       swept.freed_bytes);
    ++impl_->stats.gc_collections;
    impl_->stats.gc_freed_bytes += swept.freed_bytes;
    impl_->stats.gc_host_destructors_run += swept.host_destructors_run;
    impl_->stats.gc_peak_allocated_bytes = std::max(
        impl_->stats.gc_peak_allocated_bytes,
        mark.live_bytes + mark.garbage_bytes);
    const auto pause_ticks = mark.live_objects + mark.garbage_objects;
    impl_->stats.gc_pause_ticks += pause_ticks;
    if (impl_->logger != nullptr) {
        impl_->logger->Write(
            core::LogLevel::info, "runtime.dexvm.gc",
            "trigger=" + std::string(trigger) +
                " live_bytes=" + std::to_string(mark.live_bytes) +
                " freed_bytes=" + std::to_string(swept.freed_bytes) +
                " live_objects=" + std::to_string(mark.live_objects) +
                " freed_objects=" + std::to_string(swept.freed_objects) +
                " host_destructors_run=" +
                std::to_string(swept.host_destructors_run) +
                " pause_ticks=" + std::to_string(pause_ticks));
    }
    return swept;
}

void Interpreter::Impl::PrepareSafeAllocation(
    const std::uint64_t request_bytes, const std::string_view trigger) {
    if (!model->ShouldCollectFor(request_bytes)) return;
    static_cast<void>(owner->CollectGarbage(trigger));
}

std::uint32_t Interpreter::CurrentNativeDepth() const {
    return impl_->Execution().native_depth;
}

std::optional<DexClassId> Interpreter::CurrentCallerClass() const {
    const auto& frames = impl_->Execution().frames;
    if (frames.empty()) return std::nullopt;
    return frames.back().method->owner;
}

void Interpreter::AttachNativeThread(const std::uint64_t guest_thread_id,
                                     const std::uint64_t execution_token) {
    if (impl_->bridge != nullptr) {
        impl_->bridge->AttachThread(guest_thread_id, execution_token);
    }
}

void Interpreter::DetachNativeThread(const std::uint64_t guest_thread_id,
                                     const std::uint64_t execution_token) noexcept {
    if (impl_->bridge != nullptr) {
        impl_->bridge->DetachThread(guest_thread_id, execution_token);
    }
}

core::CapabilityLedger* Interpreter::Ledger() const noexcept {
    return impl_->ledger;
}

// ---- monitors ---------------------------------------------------------

VmMonitorTable& Interpreter::Monitors() noexcept { return *impl_->monitors; }

std::uint64_t Interpreter::CurrentContextToken() const {
    return impl_->Execution().token;
}

void Interpreter::NotifyMonitor(const VmObjectRef receiver,
                                const bool all) const {
    if (!receiver.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "notify on null"};
    }
    const auto owner = impl_->Execution().token;
    impl_->RecordTrace(DexVmTraceKind::monitor_notify, impl_->Execution(),
                       nullptr, 0, 0, receiver.Value());
    if (all) {
        impl_->monitors->NotifyAll(receiver, owner);
    } else {
        impl_->monitors->Notify(receiver, owner);
    }
}

void Interpreter::WaitOnMonitor(const VmObjectRef receiver,
                                const std::int64_t timeout_millis) const {
    if (!receiver.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "wait on null"};
    }
    impl_->RecordTrace(DexVmTraceKind::monitor_wait, impl_->Execution(),
                       nullptr, 0, 0, receiver.Value());
    const auto outcome = impl_->monitors->Wait(
        receiver, impl_->Execution().token, timeout_millis);
    switch (outcome) {
        case VmWaitOutcome::notified:
        case VmWaitOutcome::timed_out:
            return;
        case VmWaitOutcome::interrupted:
            // Thrown only after the monitor was re-acquired and its
            // recursion restored (AOSP vm/Sync.cpp waitMonitor).
            throw VmJavaThrow{"Ljava/lang/InterruptedException;",
                              "wait interrupted"};
        case VmWaitOutcome::shut_down:
            throw DexVmError(DexVmErrorReason::thread_stopped,
                             "Object.wait woke because the VM is shutting "
                             "down");
    }
}

}  // namespace ogplay::runtime::dexvm
