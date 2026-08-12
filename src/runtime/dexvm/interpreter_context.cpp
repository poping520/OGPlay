#include "ogplay/runtime/dexvm/interpreter.h"

#include <unordered_map>

#include "interpreter_internal.h"

namespace ogplay::runtime::dexvm {

namespace {

thread_local std::unordered_map<const void*, InterpreterExecutionState*>
    active_executions;

}  // namespace

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
    return *executions.at(1U);
}

const InterpreterExecutionState& Interpreter::Impl::Execution() const {
    const auto active = active_executions.find(this);
    if (active != active_executions.end()) return *active->second;
    return *executions.at(1U);
}

InterpreterExecutionState& Interpreter::Impl::Execution(
    const InterpreterExecutionContext& context) {
    if (!context.BelongsTo(owner)) {
        throw DexVmError(DexVmErrorReason::invalid_operand,
                         "execution context belongs to another interpreter");
    }
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
    context.token_ = impl_->next_execution_token++;
    auto execution = std::make_unique<InterpreterExecutionState>();
    execution->token = context.token_;
    impl_->executions.emplace(context.token_, std::move(execution));
    return context;
}

InterpreterExecutionSnapshot Interpreter::ExecutionSnapshot(
    const InterpreterExecutionContext& context) const {
    const auto& execution = impl_->Execution(context);
    InterpreterExecutionSnapshot snapshot;
    snapshot.frame_depth = execution.frames.size();
    snapshot.has_pending_exception = execution.pending_exception.IsValid();
    snapshot.ticks = execution.ticks;
    for (const auto& [_, recursion] : execution.monitors) {
        if (recursion > 0) ++snapshot.held_monitor_count;
    }
    return snapshot;
}

}  // namespace ogplay::runtime::dexvm
