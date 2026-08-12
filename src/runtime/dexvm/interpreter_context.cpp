#include "ogplay/runtime/dexvm/interpreter.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "interpreter_internal.h"
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
    std::unique_lock lock(impl_->mutex);
    if (impl_->depth == 0 || impl_->owner != std::this_thread::get_id()) {
        return 0;
    }
    const auto held = impl_->depth;
    impl_->depth = 0;
    impl_->owner = std::thread::id{};
    lock.unlock();
    impl_->released.notify_all();
    return held;
}

void VmExecutionLock::ReacquireAfterBlocking(const std::size_t depth) {
    if (depth == 0) return;
    Acquire();
    std::lock_guard lock(impl_->mutex);
    impl_->depth = depth;
}

bool VmExecutionLock::HeldByCurrentThread() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->depth > 0 && impl_->owner == std::this_thread::get_id();
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
}

VmExecutionLock& Interpreter::ExecutionLock() noexcept {
    return impl_->execution_lock;
}

std::uint32_t Interpreter::CurrentNativeDepth() const {
    return impl_->Execution().native_depth;
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
