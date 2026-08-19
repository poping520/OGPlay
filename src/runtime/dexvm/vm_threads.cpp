// Guest Java threads on real host threads (04 §3). Thread state transitions
// follow AOSP vm/Thread.cpp: start publishes a running thread, interrupt
// raises a flag and wakes a parked thread, teardown interrupts before it
// joins. Bytecode stays serialized by the interpreter execution lock.

#include "ogplay/runtime/dexvm/vm_threads.h"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "ogplay/hal/thread.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::runtime::dexvm {
namespace {

struct VmThreadRecord final {
    std::uint64_t id{};
    VmObjectRef object;
    VmObjectRef target;
    VmMethodId run_method;
    std::string name;
    InterpreterExecutionContext context;
    std::unique_ptr<hal::HostThread> host;
    VmThreadStatus status{VmThreadStatus::created};
};

// Per host thread routing, keyed by runtime so independent VMs in one test
// process cannot see each other's threads.
thread_local std::unordered_map<const void*, VmThreadRecord*> current_records;

}  // namespace

class VmThreadRuntime::Impl final {
public:
    Interpreter* vm{};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<std::uint32_t, std::unique_ptr<VmThreadRecord>> records;
    std::uint64_t next_id{2};  // 1 is the root lifecycle thread
    bool shutting_down{};
    std::string failure;

    [[nodiscard]] VmThreadRecord* Find(const VmObjectRef object) const {
        const auto found = records.find(object.Value());
        return found == records.end() ? nullptr : found->second.get();
    }

    // Guest native frames sit on the single root guest stack, so a thread
    // holding one cannot be parked: another thread would reuse that stack.
    void RefuseParkingWithNativeFrame(const char* what) const {
        if (vm->CurrentNativeDepth() == 0) return;
        if (auto* ledger = vm->Ledger(); ledger != nullptr) {
            ledger->RecordUnimplemented("dexvm.threads.block_in_native", 0);
        }
        throw DexVmError(DexVmErrorReason::blocking_in_native,
                         std::string(what) +
                             " cannot park a thread that has a live guest "
                             "native frame on the root guest stack");
    }

    // Parks the calling thread until ready() holds. The execution lock is
    // fully released while parked and restored to the same depth after.
    // Interruptible, like Thread.join on device.
    void ParkUntil(const std::function<bool()>& ready, const char* what) {
        const auto self = vm->CurrentContextToken();
        auto& monitors = vm->Monitors();
        {
            const std::lock_guard guard(mutex);
            if (shutting_down || ready()) return;
        }
        RefuseParkingWithNativeFrame(what);
        auto& lock = vm->ExecutionLock();
        const auto depth = lock.ReleaseForBlocking();
        bool interrupted = false;
        bool satisfied = false;
        {
            std::unique_lock guard(mutex);
            changed.wait(guard, [&] {
                interrupted = monitors.Interrupted(self);
                satisfied = ready();
                return shutting_down || interrupted || satisfied;
            });
        }
        lock.ReacquireAfterBlocking(depth);
        if (interrupted && !satisfied) {
            static_cast<void>(monitors.ClearInterrupt(self));
            throw VmJavaThrow{"Ljava/lang/InterruptedException;",
                              std::string(what) + " was interrupted"};
        }
    }

    void Body(VmThreadRecord* record) {
        current_records[vm] = record;
        std::string failure_text;
        auto final_status = VmThreadStatus::finished;
        try {
            const VmExecutionLockScope guard(vm->ExecutionLock());
            {
                const std::lock_guard locked(mutex);
                record->status = VmThreadStatus::running;
            }
            const std::vector<VmValue> arguments{VmValue::Ref(record->target)};
            const auto outcome =
                vm->Call(record->context, record->run_method, arguments);
            if (outcome.exception.IsValid()) {
                final_status = VmThreadStatus::failed;
                failure_text = "uncaught exception on Java thread " +
                               record->name + ": " +
                               vm->Linker()
                                   .Class(outcome.exception_class)
                                   .descriptor +
                               ": " + outcome.exception_message;
                for (const auto& entry : outcome.exception_stack) {
                    failure_text += "\n  at " + entry.class_descriptor + "." +
                                    entry.method_name + " (pc " +
                                    std::to_string(entry.pc) + ")";
                }
            }
        } catch (const DexVmError& error) {
            if (error.Reason() == DexVmErrorReason::thread_stopped) {
                final_status = VmThreadStatus::stopped;
            } else {
                final_status = VmThreadStatus::failed;
                failure_text = "Java thread " + record->name +
                               " failed: " + error.what();
            }
        } catch (const std::exception& error) {
            final_status = VmThreadStatus::failed;
            failure_text =
                "Java thread " + record->name + " failed: " + error.what();
        }
        // A dead thread must not keep anybody out of the monitors it still
        // held, and its wait-set membership goes with it.
        vm->Monitors().ReleaseAll(record->context.Token());
        current_records.erase(vm);
        {
            const std::lock_guard locked(mutex);
            record->status = final_status;
            if (!failure_text.empty() && failure.empty()) {
                failure = std::move(failure_text);
            }
            if (final_status == VmThreadStatus::failed) {
                // Uncaught death is fatal to the process on device. Threads
                // waiting on a handshake this one will never complete must
                // not be left parked forever: bring the VM down so the
                // recorded reason is what the session reports.
                shutting_down = true;
            }
        }
        if (final_status == VmThreadStatus::failed) vm->Monitors().Shutdown();
        changed.notify_all();
    }
};

VmThreadRuntime::VmThreadRuntime(Interpreter& vm)
    : impl_(std::make_unique<Impl>()) {
    impl_->vm = &vm;
}

VmThreadRuntime::~VmThreadRuntime() {
    try {
        Shutdown();
    } catch (const std::exception&) {
        // Teardown is best effort; the host threads are already stopped.
    }
}

void VmThreadRuntime::Start(const VmObjectRef thread_object,
                            const VmObjectRef run_target, std::string name) {
    if (!thread_object.IsValid() || !run_target.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "Thread.start() has no target"};
    }
    auto& linker = impl_->vm->Linker();
    const auto target_class = impl_->vm->Model().ObjectClass(run_target);
    const auto index = linker.FindVtableIndex(target_class, "run", "()V");
    if (!index.has_value()) {
        throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                          "thread target has no run(): " +
                              linker.Class(target_class).descriptor};
    }
    const auto run_method = linker.Class(target_class).vtable[*index];

    const std::lock_guard guard(impl_->mutex);
    if (impl_->shutting_down) {
        throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                          "the VM is shutting down"};
    }
    if (const auto* existing = impl_->Find(thread_object);
        existing != nullptr) {
        throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                          "thread started twice: " + existing->name};
    }
    auto created = std::make_unique<VmThreadRecord>();
    created->id = impl_->next_id++;
    created->object = thread_object;
    created->target = run_target;
    created->run_method = run_method;
    created->name = name.empty() ? "Thread-" + std::to_string(created->id)
                                 : std::move(name);
    created->context = impl_->vm->CreateExecutionContext();
    auto* record = created.get();
    impl_->records.emplace(thread_object.Value(), std::move(created));
    // Published under the same lock the teardown join reads it under; the
    // new thread blocks on the execution lock before it needs anything here.
    record->host = hal::StartHostThread(
        [impl = impl_.get(), record] { impl->Body(record); });
}

bool VmThreadRuntime::IsAlive(const VmObjectRef thread_object) const {
    const std::lock_guard guard(impl_->mutex);
    const auto* record = impl_->Find(thread_object);
    if (record == nullptr) return false;
    return record->status == VmThreadStatus::created ||
           record->status == VmThreadStatus::running;
}

std::uint64_t VmThreadRuntime::ThreadId(const VmObjectRef thread_object) const {
    const std::lock_guard guard(impl_->mutex);
    const auto* record = impl_->Find(thread_object);
    return record == nullptr ? 0U : record->id;
}

void VmThreadRuntime::Join(const VmObjectRef thread_object) {
    VmThreadRecord* record{};
    {
        const std::lock_guard guard(impl_->mutex);
        record = impl_->Find(thread_object);
        if (record == nullptr) return;  // never started or already reaped
        const auto found = current_records.find(impl_->vm);
        if (found != current_records.end() && found->second == record) {
            throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                              "a thread cannot join itself"};
        }
    }
    impl_->ParkUntil(
        [record] {
            return record->status != VmThreadStatus::created &&
                   record->status != VmThreadStatus::running;
        },
        "Thread.join()");
}

void VmThreadRuntime::Interrupt(const VmObjectRef thread_object) {
    std::uint64_t token = 0;
    {
        const std::lock_guard guard(impl_->mutex);
        const auto* record = impl_->Find(thread_object);
        if (record == nullptr) return;
        token = record->context.Token();
    }
    // The monitor table owns the interrupt flag so a thread parked in
    // Object.wait and one parked in join observe the same fact.
    impl_->vm->Monitors().Interrupt(token);
    impl_->changed.notify_all();
}

bool VmThreadRuntime::IsInterrupted(const VmObjectRef thread_object) const {
    std::uint64_t token = 0;
    {
        const std::lock_guard guard(impl_->mutex);
        const auto* record = impl_->Find(thread_object);
        if (record == nullptr) return false;
        token = record->context.Token();
    }
    return impl_->vm->Monitors().Interrupted(token);
}

bool VmThreadRuntime::ClearCurrentInterrupt() {
    return impl_->vm->Monitors().ClearInterrupt(
        impl_->vm->CurrentContextToken());
}

void VmThreadRuntime::Yield() {
    auto& lock = impl_->vm->ExecutionLock();
    if (!lock.HeldByCurrentThread()) return;
    if (impl_->vm->CurrentNativeDepth() != 0) return;
    {
        const std::lock_guard guard(impl_->mutex);
        if (impl_->records.empty()) return;
    }
    const auto depth = lock.ReleaseForBlocking();
    lock.ReacquireAfterBlocking(depth);
}

std::optional<std::string> VmThreadRuntime::TakeFailure() {
    const std::lock_guard guard(impl_->mutex);
    if (impl_->failure.empty()) return std::nullopt;
    return std::exchange(impl_->failure, {});
}

VmObjectRef VmThreadRuntime::CurrentThreadObject() const {
    const auto found = current_records.find(impl_->vm);
    if (found == current_records.end()) return VmObjectRef{};
    return found->second->object;
}

std::size_t VmThreadRuntime::LiveCount() const {
    const std::lock_guard guard(impl_->mutex);
    std::size_t live = 0;
    for (const auto& [_, record] : impl_->records) {
        if (record->status == VmThreadStatus::created ||
            record->status == VmThreadStatus::running) {
            ++live;
        }
    }
    return live;
}

std::vector<VmThreadSnapshot> VmThreadRuntime::Snapshot() const {
    const std::lock_guard guard(impl_->mutex);
    std::vector<VmThreadSnapshot> snapshot;
    snapshot.reserve(impl_->records.size());
    for (const auto& [_, record] : impl_->records) {
        snapshot.push_back(
            {record->id, record->object.Value(), record->status,
             impl_->vm->Monitors().Interrupted(record->context.Token()),
             record->name});
    }
    return snapshot;
}

void VmThreadRuntime::VisitThreadRoots(
    const std::function<void(VmObjectRef)>& visitor) const {
    if (!visitor) return;
    const std::lock_guard guard(impl_->mutex);
    for (const auto& [_, record] : impl_->records) {
        visitor(record->object);
        visitor(record->target);
    }
}

bool VmThreadRuntime::ShuttingDown() const {
    const std::lock_guard guard(impl_->mutex);
    return impl_->shutting_down;
}

void VmThreadRuntime::Shutdown() {
    std::vector<VmThreadRecord*> live;
    {
        const std::lock_guard guard(impl_->mutex);
        impl_->shutting_down = true;
        for (const auto& [_, record] : impl_->records) {
            if (record->host) live.push_back(record.get());
        }
    }
    // Interrupt before join, matching the existing guest teardown order.
    // Waiters have to come out of their wait sets too, or the stop check
    // never gets a chance to run.
    for (auto* record : live) {
        impl_->vm->RequestStop(record->context);
    }
    impl_->vm->Monitors().Shutdown();
    impl_->changed.notify_all();

    // Joining needs the children to make progress, so the caller must not
    // keep holding the execution lock across it.
    auto& lock = impl_->vm->ExecutionLock();
    const auto depth = lock.ReleaseForBlocking();
    for (auto* record : live) {
        if (record->host && record->host->Joinable()) record->host->Join();
    }
    lock.ReacquireAfterBlocking(depth);

    // Records stay so teardown remains inspectable (status, name, failure);
    // only the host thread and the execution context are released.
    const std::lock_guard guard(impl_->mutex);
    for (auto* record : live) {
        record->host.reset();
        impl_->vm->UnwindStoppedExecutionContext(record->context);
        impl_->vm->DiscardExecutionContext(record->context);
    }
}

}  // namespace ogplay::runtime::dexvm
