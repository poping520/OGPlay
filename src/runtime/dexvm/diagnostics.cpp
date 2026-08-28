#include "ogplay/runtime/dexvm/diagnostics.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#include "interpreter_internal.h"
#include "ogplay/core/json.h"
#include "ogplay/runtime/dexvm/vm_threads.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::string_view ThreadStatusName(
    const VmThreadStatus status) noexcept {
    switch (status) {
        case VmThreadStatus::created: return "created";
        case VmThreadStatus::running: return "running";
        case VmThreadStatus::finished: return "finished";
        case VmThreadStatus::stopped: return "stopped";
        case VmThreadStatus::failed: return "failed";
    }
    return "unknown";
}

[[nodiscard]] bool Contains(const std::string_view value,
                            const std::string_view filter) {
    return filter.empty() || value.find(filter) != std::string_view::npos;
}

}  // namespace

std::string_view DexVmTraceKindName(const DexVmTraceKind kind) noexcept {
    switch (kind) {
        case DexVmTraceKind::instruction: return "instruction";
        case DexVmTraceKind::method_enter: return "method_enter";
        case DexVmTraceKind::method_exit: return "method_exit";
        case DexVmTraceKind::exception_throw: return "exception_throw";
        case DexVmTraceKind::exception_catch: return "exception_catch";
        case DexVmTraceKind::class_init_begin: return "class_init_begin";
        case DexVmTraceKind::class_init_end: return "class_init_end";
        case DexVmTraceKind::class_init_fail: return "class_init_fail";
        case DexVmTraceKind::monitor_enter: return "monitor_enter";
        case DexVmTraceKind::monitor_exit: return "monitor_exit";
        case DexVmTraceKind::monitor_wait: return "monitor_wait";
        case DexVmTraceKind::monitor_notify: return "monitor_notify";
        case DexVmTraceKind::native_enter: return "native_enter";
        case DexVmTraceKind::native_exit: return "native_exit";
        case DexVmTraceKind::gc_begin: return "gc_begin";
        case DexVmTraceKind::gc_end: return "gc_end";
    }
    return "unknown";
}

void Interpreter::Impl::RecordTrace(
    const DexVmTraceKind kind, const InterpreterExecutionState& execution,
    const LinkedMethod* const method, const std::uint32_t dex_pc,
    const std::uint8_t opcode, const std::uint64_t value) {
    if (trace_ring.empty() ||
        (config.diagnostics.event_mask & DexVmTraceBit(kind)) == 0U) {
        return;
    }
    if (kind == DexVmTraceKind::instruction &&
        execution.ticks % config.diagnostics.instruction_sample_interval != 0U) {
        return;
    }
    RawDexVmTraceEntry entry;
    entry.sequence = ++trace_sequence;
    entry.kind = kind;
    entry.context_token = execution.token;
    entry.tick = execution.ticks;
    entry.java_class = method == nullptr ? DexClassId{} : method->owner;
    entry.method = method == nullptr ? VmMethodId{} : method->id;
    entry.dex_pc = dex_pc;
    entry.opcode = opcode;
    entry.value = value;
    trace_ring[trace_head] = entry;
    trace_head = (trace_head + 1U) % trace_ring.size();
    trace_size = std::min(trace_size + 1U, trace_ring.size());
}

bool Interpreter::DiagnosticsEnabled() const noexcept {
    return !impl_->trace_ring.empty();
}

std::vector<DexVmTraceEntry> Interpreter::Trace(
    const std::string_view filter, const std::size_t limit) const {
    if (limit == 0U || limit > 10000U) {
        throw std::invalid_argument("DexVM trace limit must be in 1..10000");
    }
    VmExecutionLockScope execution_scope(impl_->execution_lock);
    std::vector<DexVmTraceEntry> result;
    result.reserve(std::min(limit, impl_->trace_size));
    for (std::size_t offset = 0; offset < impl_->trace_size; ++offset) {
        const auto index =
            (impl_->trace_head + impl_->trace_ring.size() - 1U - offset) %
            impl_->trace_ring.size();
        const auto& raw = impl_->trace_ring[index];
        DexVmTraceEntry entry;
        entry.sequence = raw.sequence;
        entry.kind = raw.kind;
        entry.context_token = raw.context_token;
        entry.tick = raw.tick;
        entry.dex_pc = raw.dex_pc;
        entry.opcode = raw.opcode;
        entry.value = raw.value;
        if (raw.method.IsValid()) {
            const auto& method = impl_->linker->Method(raw.method);
            entry.class_descriptor =
                impl_->linker->Class(method.owner).descriptor;
            entry.method_name = method.name;
            entry.method_descriptor = method.descriptor;
        } else if ((raw.kind == DexVmTraceKind::class_init_begin ||
                    raw.kind == DexVmTraceKind::class_init_end ||
                    raw.kind == DexVmTraceKind::class_init_fail) &&
                   raw.value != 0U) {
            entry.class_descriptor =
                impl_->linker->Class(DexClassId(
                    static_cast<std::uint32_t>(raw.value))).descriptor;
        }
        if (!Contains(DexVmTraceKindName(entry.kind), filter) &&
            !Contains(entry.class_descriptor, filter) &&
            !Contains(entry.method_name, filter) &&
            !Contains(entry.method_descriptor, filter)) {
            continue;
        }
        result.push_back(std::move(entry));
        if (result.size() == limit) break;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<DexVmThreadStack> Interpreter::StackSnapshot() const {
    VmExecutionLockScope execution_scope(impl_->execution_lock);
    std::unordered_map<std::uint64_t, VmThreadSnapshot> thread_by_context;
    if (impl_->threads != nullptr) {
        for (auto thread : impl_->threads->Snapshot()) {
            thread_by_context.emplace(thread.context_token, std::move(thread));
        }
    }
    const std::lock_guard contexts_lock(impl_->executions_mutex);
    return impl_->BuildStackSnapshotLocked(thread_by_context);
}

std::vector<DexVmThreadStack> Interpreter::Impl::BuildStackSnapshotLocked(
    const std::unordered_map<std::uint64_t, VmThreadSnapshot>&
        thread_by_context) const {
    std::vector<DexVmThreadStack> result;
    result.reserve(executions.size());
    for (const auto& [token, execution] : executions) {
        DexVmThreadStack stack;
        stack.context_token = token;
        stack.ticks = execution->ticks;
        if (const auto found = thread_by_context.find(token);
            found != thread_by_context.end()) {
            stack.guest_thread_id = found->second.id;
            stack.thread_name = found->second.name;
            stack.thread_status = std::string(ThreadStatusName(found->second.status));
        } else {
            stack.thread_status = execution->frames.empty() ? "idle" : "running";
        }
        if (execution->pending_exception.IsValid()) {
            const auto java_class =
                model->ObjectClass(execution->pending_exception);
            if (java_class.IsValid()) {
                stack.pending_exception =
                    linker->Class(java_class).descriptor;
            }
        }
        stack.frames.reserve(execution->frames.size());
        for (const auto& frame : execution->frames) {
            stack.frames.push_back(
                {linker->Class(frame.method->owner).descriptor,
                 frame.method->name, frame.method->descriptor, frame.pc});
        }
        result.push_back(std::move(stack));
    }
    std::ranges::sort(result, {}, &DexVmThreadStack::context_token);
    return result;
}

std::optional<std::vector<DexVmTraceEntry>> Interpreter::TryTrace(
    const std::size_t limit) const {
    if (!impl_->execution_lock.TryAcquire()) return std::nullopt;
    struct Release final {
        VmExecutionLock* lock;
        ~Release() { lock->Release(); }
    } release{&impl_->execution_lock};
    return Trace({}, limit);
}

std::optional<std::vector<DexVmThreadStack>>
Interpreter::TryStackSnapshot() const {
    if (!impl_->execution_lock.TryAcquire()) return std::nullopt;
    struct Release final {
        VmExecutionLock* lock;
        ~Release() { lock->Release(); }
    } release{&impl_->execution_lock};
    std::unordered_map<std::uint64_t, VmThreadSnapshot> thread_by_context;
    if (impl_->threads != nullptr) {
        auto threads = impl_->threads->TrySnapshot();
        if (!threads) return std::nullopt;
        for (auto& thread : *threads) {
            thread_by_context.emplace(thread.context_token, std::move(thread));
        }
    }
    std::unique_lock contexts_lock(impl_->executions_mutex, std::try_to_lock);
    if (!contexts_lock.owns_lock()) return std::nullopt;
    return impl_->BuildStackSnapshotLocked(thread_by_context);
}

std::string RenderDexVmTraceJson(
    const std::vector<DexVmTraceEntry>& entries) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddInteger(root, "schema", 1);
    const auto values = writer.Array();
    for (const auto& entry : entries) {
        const auto value = writer.Object();
        writer.AddUnsignedInteger(value, "sequence", entry.sequence);
        writer.AddString(value, "event", DexVmTraceKindName(entry.kind));
        writer.AddUnsignedInteger(value, "context", entry.context_token);
        writer.AddUnsignedInteger(value, "tick", entry.tick);
        writer.AddString(value, "class", entry.class_descriptor);
        writer.AddString(value, "method", entry.method_name);
        writer.AddString(value, "descriptor", entry.method_descriptor);
        writer.AddUnsignedInteger(value, "dex_pc", entry.dex_pc);
        writer.AddUnsignedInteger(value, "opcode", entry.opcode);
        writer.AddUnsignedInteger(value, "value", entry.value);
        writer.Append(values, value);
    }
    writer.Add(root, "entries", values);
    return writer.Serialize(root);
}

std::string RenderDexVmStacksJson(
    const std::vector<DexVmThreadStack>& stacks) {
    core::JsonWriter writer;
    const auto root = writer.Object();
    writer.AddInteger(root, "schema", 1);
    const auto threads = writer.Array();
    for (const auto& stack : stacks) {
        const auto thread = writer.Object();
        writer.AddUnsignedInteger(thread, "context", stack.context_token);
        writer.AddUnsignedInteger(thread, "guest_thread_id",
                                  stack.guest_thread_id);
        writer.AddString(thread, "name", stack.thread_name);
        writer.AddString(thread, "status", stack.thread_status);
        writer.AddUnsignedInteger(thread, "ticks", stack.ticks);
        writer.AddString(thread, "pending_exception", stack.pending_exception);
        const auto frames = writer.Array();
        for (const auto& frame : stack.frames) {
            const auto item = writer.Object();
            writer.AddString(item, "class", frame.class_descriptor);
            writer.AddString(item, "method", frame.method_name);
            writer.AddString(item, "descriptor", frame.method_descriptor);
            writer.AddUnsignedInteger(item, "dex_pc", frame.dex_pc);
            writer.Append(frames, item);
        }
        writer.Add(thread, "frames", frames);
        writer.Append(threads, thread);
    }
    writer.Add(root, "threads", threads);
    return writer.Serialize(root);
}

}  // namespace ogplay::runtime::dexvm
