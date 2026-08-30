// Guest Java threads on real host threads (04 §3). Thread state transitions
// follow AOSP vm/Thread.cpp: start publishes a running thread, interrupt
// raises a flag and wakes a parked thread, teardown interrupts before it
// joins. Bytecode stays serialized by the interpreter execution lock.

#include "ogplay/runtime/dexvm/vm_threads.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include "ogplay/hal/thread.h"
#include "ogplay/runtime/dexvm/vm_monitors.h"

namespace ogplay::runtime::dexvm {
namespace {

[[nodiscard]] std::string IndentDiagnostic(const std::string_view text,
                                           const std::string_view indent) {
    std::string result{indent};
    for (const char character : text) {
        result += character;
        if (character == '\n') result += indent;
    }
    return result;
}

struct VmThreadRecord final {
    std::uint64_t id{};
    VmObjectRef object;
    VmMethodId run_method;
    VmThreadRuntime::UncaughtExceptionDispatcher uncaught_dispatcher;
    std::string name;
    InterpreterExecutionContext context;
    std::unique_ptr<hal::HostThread> host;
    VmThreadStatus status{VmThreadStatus::created};
    VmThreadWaitState wait_state{VmThreadWaitState::none};
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
    VmObjectRef root_object;
    std::string root_name{"main"};
    std::uint64_t next_id{2};  // 1 is the root lifecycle thread
    VmThreadWaitState root_wait_state{VmThreadWaitState::none};
    std::uint64_t progress_generation{};
    bool shutting_down{};
    std::string failure;

    [[nodiscard]] VmThreadRecord* Find(const VmObjectRef object) const {
        const auto found = records.find(object.Value());
        return found == records.end() ? nullptr : found->second.get();
    }

    [[nodiscard]] std::string NameOf(const VmThreadRecord* record) const {
        const std::lock_guard guard(mutex);
        return record->name;
    }

    // Failure reports pair the Java-visible name with the DexVM thread
    // record id so repeat names ("Thread-2") stay distinguishable.
    [[nodiscard]] std::string DescribeForReport(
        const VmThreadRecord* record) const {
        const std::lock_guard guard(mutex);
        return record->name + " (id " + std::to_string(record->id) + ")";
    }

    // Parks the calling thread until ready() holds. The execution lock is
    // fully released while parked and restored to the same depth after.
    // Interruptible, like Thread.join on device.
    void ParkUntil(const std::function<bool()>& ready, const char* what,
                   const std::optional<std::int64_t> timeout_millis,
                   const VmThreadWaitState wait_state) {
        const auto self = vm->CurrentContextToken();
        auto& monitors = vm->Monitors();
        VmMonotonicMillis clock;
        {
            const std::lock_guard guard(mutex);
            if (shutting_down || ready()) return;
        }
        if (monitors.Interrupted(self)) {
            static_cast<void>(monitors.ClearInterrupt(self));
            throw VmJavaThrow{"Ljava/lang/InterruptedException;",
                              std::string(what) + " was interrupted"};
        }
        if (timeout_millis.has_value()) {
            clock = monitors.TimeSource();
            if (!clock) {
                if (auto* ledger = vm->Ledger(); ledger != nullptr) {
                    ledger->RecordUnimplemented(
                        "dexvm.threads.wait_without_clock", 0);
                }
                throw DexVmError(
                    DexVmErrorReason::internal_invariant,
                    std::string(what) +
                        " needs the unified Clock; this session published "
                        "no time source");
            }
        }
        std::int64_t deadline = 0;
        if (timeout_millis.has_value()) {
            const auto now = clock();
            deadline = *timeout_millis >
                               std::numeric_limits<std::int64_t>::max() - now
                           ? std::numeric_limits<std::int64_t>::max()
                           : now + *timeout_millis;
        }
        vm->Threads().SetWaitState(self, wait_state);
        auto& lock = vm->ExecutionLock();
        const auto depth = lock.ReleaseForBlocking();
        bool interrupted = false;
        bool advanced = false;
        {
            std::unique_lock guard(mutex);
            while (!shutting_down && !ready()) {
                interrupted = monitors.Interrupted(self);
                if (interrupted) break;
                if (timeout_millis.has_value() && clock() >= deadline) break;
                if (timeout_millis.has_value()) {
                    changed.wait_for(guard, std::chrono::milliseconds(2));
                    if (!advanced) {
                        guard.unlock();
                        advanced = monitors.TryAdvanceClockForTimedPark(
                            self, deadline);
                        guard.lock();
                    }
                } else {
                    changed.wait(guard);
                }
            }
        }
        lock.ReacquireAfterBlocking(depth);
        vm->Threads().SetWaitState(self, VmThreadWaitState::none);
        if (interrupted) {
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
            vm->AttachNativeThread(record->id, record->context.Token());
            const VmExecutionLockScope guard(vm->ExecutionLock());
            {
                const std::lock_guard locked(mutex);
                record->status = VmThreadStatus::running;
                ++progress_generation;
            }
            changed.notify_all();
            const std::vector<VmValue> arguments{VmValue::Ref(record->object)};
            const auto outcome =
                vm->Call(record->context, record->run_method, arguments);
            if (outcome.exception.IsValid()) {
                const auto handled = record->uncaught_dispatcher &&
                                     record->uncaught_dispatcher(
                                         *vm, record->object,
                                         outcome.exception);
                if (!handled) {
                    final_status = VmThreadStatus::failed;
                    failure_text = "uncaught exception on Java thread " +
                                   DescribeForReport(record) + ": " +
                                   vm->Linker()
                                       .Class(outcome.exception_class)
                                       .descriptor +
                                   ": " + outcome.exception_message;
                    for (const auto& entry : outcome.exception_stack) {
                        failure_text +=
                            "\n  at " + entry.class_descriptor + "." +
                            entry.method_name + " (pc " +
                            std::to_string(entry.pc) + ")";
                    }
                }
            }
        } catch (const DexVmError& error) {
            if (error.Reason() == DexVmErrorReason::thread_stopped) {
                final_status = VmThreadStatus::stopped;
            } else {
                final_status = VmThreadStatus::failed;
                failure_text = "Java thread " + DescribeForReport(record) +
                               " failed:\n" +
                               IndentDiagnostic(error.what(), "  ");
            }
        } catch (const std::exception& error) {
            final_status = VmThreadStatus::failed;
            failure_text = "Java thread " + DescribeForReport(record) +
                           " failed:\n" +
                           IndentDiagnostic(error.what(), "  ");
        }
        // A dead thread must not keep anybody out of the monitors it still
        // held, and its wait-set membership goes with it.
        vm->Monitors().ReleaseAll(record->context.Token());
        vm->DetachNativeThread(record->id, record->context.Token());
        current_records.erase(vm);
        {
            const std::lock_guard locked(mutex);
            record->status = final_status;
            record->wait_state = VmThreadWaitState::none;
            ++progress_generation;
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
    vm.SetThreadRuntime(this);
}

VmThreadRuntime::~VmThreadRuntime() {
    try {
        Shutdown();
    } catch (const std::exception&) {
        // Teardown is best effort; the host threads are already stopped.
    }
    impl_->vm->SetThreadRuntime(nullptr);
}

std::uint64_t VmThreadRuntime::AllocateThreadId() {
    const std::lock_guard guard(impl_->mutex);
    return impl_->next_id++;
}

void VmThreadRuntime::SetRootThreadObject(const VmObjectRef thread_object) {
    if (!thread_object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "root Thread object is null"};
    }
    const std::lock_guard guard(impl_->mutex);
    if (impl_->root_object.IsValid() && impl_->root_object != thread_object) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "root Thread object identity changed");
    }
    impl_->root_object = thread_object;
}

void VmThreadRuntime::Start(const VmObjectRef thread_object, std::string name,
                            const std::uint64_t thread_id,
                            UncaughtExceptionDispatcher uncaught_dispatcher) {
    if (!thread_object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "Thread.start() receiver is null"};
    }
    if (thread_id <= 1U) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "guest Thread has no allocated per-VM id");
    }
    auto& linker = impl_->vm->Linker();
    const auto thread_class = impl_->vm->Model().ObjectClass(thread_object);
    const auto index = linker.FindVtableIndex(thread_class, "run", "()V");
    if (!index.has_value()) {
        throw VmJavaThrow{"Ljava/lang/IllegalThreadStateException;",
                          "thread object has no run(): " +
                              linker.Class(thread_class).descriptor};
    }
    const auto run_method = linker.Class(thread_class).vtable[*index];

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
    created->id = thread_id;
    created->object = thread_object;
    created->run_method = run_method;
    created->uncaught_dispatcher = std::move(uncaught_dispatcher);
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
    if (impl_->root_object.IsValid() && thread_object == impl_->root_object) {
        return !impl_->shutting_down;
    }
    const auto* record = impl_->Find(thread_object);
    if (record == nullptr) return false;
    return record->status == VmThreadStatus::created ||
           record->status == VmThreadStatus::running;
}

std::uint64_t VmThreadRuntime::ThreadId(const VmObjectRef thread_object) const {
    const std::lock_guard guard(impl_->mutex);
    if (impl_->root_object.IsValid() && thread_object == impl_->root_object) {
        return 1U;
    }
    const auto* record = impl_->Find(thread_object);
    return record == nullptr ? 0U : record->id;
}

void VmThreadRuntime::Join(const VmObjectRef thread_object) {
    VmThreadRecord* record{};
    bool root = false;
    {
        const std::lock_guard guard(impl_->mutex);
        root = impl_->root_object.IsValid() &&
               thread_object == impl_->root_object;
        record = impl_->Find(thread_object);
        if (record == nullptr && !root) return;  // never started
    }
    impl_->ParkUntil(
        [record, root] {
            return !root && record->status != VmThreadStatus::created &&
                   record->status != VmThreadStatus::running;
        },
        "Thread.join()", std::nullopt, VmThreadWaitState::joining);
}

void VmThreadRuntime::Join(const VmObjectRef thread_object,
                           const std::int64_t timeout_millis) {
    if (timeout_millis <= 0) {
        Join(thread_object);
        return;
    }
    VmThreadRecord* record{};
    bool root = false;
    {
        const std::lock_guard guard(impl_->mutex);
        root = impl_->root_object.IsValid() &&
               thread_object == impl_->root_object;
        record = impl_->Find(thread_object);
        if (record == nullptr && !root) return;
    }
    impl_->ParkUntil(
        [record, root] {
            return !root && record->status != VmThreadStatus::created &&
                   record->status != VmThreadStatus::running;
        },
        "Thread.join()", timeout_millis, VmThreadWaitState::joining);
}

void VmThreadRuntime::Sleep(const std::int64_t timeout_millis) {
    if (timeout_millis < 0) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "timeout arguments out of range"};
    }
    const auto self = impl_->vm->CurrentContextToken();
    auto& monitors = impl_->vm->Monitors();
    if (monitors.ClearInterrupt(self)) {
        throw VmJavaThrow{"Ljava/lang/InterruptedException;",
                          "Thread.sleep() was interrupted"};
    }
    VmMonotonicMillis clock;
    std::int64_t deadline = 0;
    if (timeout_millis > 0) {
        clock = monitors.TimeSource();
        if (!clock) {
            if (auto* ledger = impl_->vm->Ledger(); ledger != nullptr) {
                ledger->RecordUnimplemented(
                    "dexvm.threads.wait_without_clock", 0);
            }
            throw DexVmError(
                DexVmErrorReason::internal_invariant,
                "Thread.sleep() needs the unified Clock; this session "
                "published no time source");
        }
        const auto now = clock();
        deadline = timeout_millis >
                           std::numeric_limits<std::int64_t>::max() - now
                       ? std::numeric_limits<std::int64_t>::max()
                       : now + timeout_millis;
    }

    if (timeout_millis > 0) {
        SetWaitState(self, VmThreadWaitState::sleeping);
    }
    auto& lock = impl_->vm->ExecutionLock();
    const auto depth = lock.ReleaseForBlocking();
    bool interrupted = false;
    bool stopped = false;
    if (timeout_millis == 0) {
        // AOSP dvmThreadSleep explicitly distinguishes sleep(0,0) from
        // Object.wait(0,0). It still yields once, but never waits forever.
        std::this_thread::yield();
    } else {
        bool advanced = false;
        std::unique_lock guard(impl_->mutex);
        while (!(stopped = impl_->shutting_down) &&
               !(interrupted = monitors.Interrupted(self)) &&
               clock() < deadline) {
            impl_->changed.wait_for(guard, std::chrono::milliseconds(2));
            if (!advanced) {
                guard.unlock();
                advanced = monitors.TryAdvanceClockForTimedPark(
                    self, deadline);
                guard.lock();
            }
        }
    }
    lock.ReacquireAfterBlocking(depth);
    if (timeout_millis > 0) {
        SetWaitState(self, VmThreadWaitState::none);
    }
    if (interrupted) {
        static_cast<void>(monitors.ClearInterrupt(self));
        throw VmJavaThrow{"Ljava/lang/InterruptedException;",
                          "Thread.sleep() was interrupted"};
    }
    if (stopped) {
        throw DexVmError(DexVmErrorReason::thread_stopped,
                         "Thread.sleep woke because the VM is shutting down");
    }
}

void VmThreadRuntime::Interrupt(const VmObjectRef thread_object) {
    std::uint64_t token = 0;
    {
        const std::lock_guard guard(impl_->mutex);
        if (impl_->root_object.IsValid() &&
            thread_object == impl_->root_object) {
            token = 1U;
        }
        const auto* record = impl_->Find(thread_object);
        if (record != nullptr &&
            (record->status == VmThreadStatus::created ||
             record->status == VmThreadStatus::running)) {
            token = record->context.Token();
        }
        if (token == 0) return;
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
        if (impl_->root_object.IsValid() &&
            thread_object == impl_->root_object) {
            token = 1U;
        }
        const auto* record = impl_->Find(thread_object);
        if (record != nullptr &&
            (record->status == VmThreadStatus::created ||
             record->status == VmThreadStatus::running)) {
            token = record->context.Token();
        }
        if (token == 0) return false;
    }
    return impl_->vm->Monitors().Interrupted(token);
}

bool VmThreadRuntime::ClearCurrentInterrupt() {
    return impl_->vm->Monitors().ClearInterrupt(
        impl_->vm->CurrentContextToken());
}

void VmThreadRuntime::Rename(const VmObjectRef thread_object,
                             std::string name) {
    const std::lock_guard guard(impl_->mutex);
    if (impl_->root_object.IsValid() && thread_object == impl_->root_object) {
        impl_->root_name = std::move(name);
        return;
    }
    if (auto* record = impl_->Find(thread_object); record != nullptr) {
        record->name = std::move(name);
    }
}

void VmThreadRuntime::Yield() {
    auto& lock = impl_->vm->ExecutionLock();
    const bool acquired_for_host_yield = !lock.HeldByCurrentThread();
    if (acquired_for_host_yield) {
        lock.Acquire();
        std::uint64_t generation{};
        {
            const std::lock_guard guard(impl_->mutex);
            const bool has_runnable = std::ranges::any_of(
                impl_->records, [](const auto& entry) {
                    return entry.second->status == VmThreadStatus::created ||
                           (entry.second->status == VmThreadStatus::running &&
                            entry.second->wait_state ==
                                VmThreadWaitState::none);
                });
            if (!has_runnable) {
                lock.Release();
                return;
            }
            generation = impl_->progress_generation;
        }
        const auto depth = lock.ReleaseForBlocking();
        std::unique_lock guard(impl_->mutex);
        impl_->changed.wait(guard, [this, generation] {
            return impl_->shutting_down ||
                   impl_->progress_generation != generation ||
                   std::ranges::none_of(
                       impl_->records, [](const auto& entry) {
                           return entry.second->status ==
                                      VmThreadStatus::created ||
                                  (entry.second->status ==
                                       VmThreadStatus::running &&
                                   entry.second->wait_state ==
                                       VmThreadWaitState::none);
                       });
        });
        guard.unlock();
        lock.ReacquireAfterBlocking(depth);
        lock.Release();
        return;
    }

    {
        const std::lock_guard guard(impl_->mutex);
        ++impl_->progress_generation;
    }
    impl_->changed.notify_all();
    const auto depth = lock.ReleaseForBlocking();
    std::this_thread::yield();
    lock.ReacquireAfterBlocking(depth);
}

void VmThreadRuntime::SetWaitState(
    const std::uint64_t context_token,
    const VmThreadWaitState wait_state) {
    {
        const std::lock_guard guard(impl_->mutex);
        if (context_token == kRootLifecycleToken) {
            if (impl_->root_wait_state == wait_state) return;
            impl_->root_wait_state = wait_state;
        } else {
            const auto found = std::ranges::find_if(
                impl_->records, [context_token](const auto& entry) {
                    return entry.second->context.Token() == context_token;
                });
            if (found == impl_->records.end()) return;
            if (found->second->wait_state == wait_state) return;
            found->second->wait_state = wait_state;
        }
        ++impl_->progress_generation;
    }
    impl_->changed.notify_all();
}

std::optional<std::string> VmThreadRuntime::TakeFailure() {
    const std::lock_guard guard(impl_->mutex);
    if (impl_->failure.empty()) return std::nullopt;
    return std::exchange(impl_->failure, {});
}

VmObjectRef VmThreadRuntime::CurrentThreadObject() const {
    const auto found = current_records.find(impl_->vm);
    if (found != current_records.end()) return found->second->object;
    if (impl_->vm->CurrentContextToken() != 1U) return VmObjectRef{};
    const std::lock_guard guard(impl_->mutex);
    return impl_->root_object;
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
    snapshot.reserve(impl_->records.size() + 1U);
    if (impl_->root_object.IsValid()) {
        snapshot.push_back({1U, 1U, impl_->root_object.Value(),
                            impl_->shutting_down ? VmThreadStatus::stopped
                                                : VmThreadStatus::running,
                            impl_->root_wait_state,
                            impl_->vm->Monitors().Interrupted(1U),
                            impl_->root_name});
    }
    for (const auto& [_, record] : impl_->records) {
        snapshot.push_back(
            {record->id, record->context.Token(), record->object.Value(), record->status,
             record->wait_state,
             impl_->vm->Monitors().Interrupted(record->context.Token()),
             record->name});
    }
    return snapshot;
}

std::optional<std::vector<VmThreadSnapshot>> VmThreadRuntime::TrySnapshot() const {
    std::unique_lock guard(impl_->mutex, std::try_to_lock);
    if (!guard.owns_lock()) return std::nullopt;
    std::vector<VmThreadSnapshot> snapshot;
    snapshot.reserve(impl_->records.size() + 1U);
    const auto append = [&](const std::uint64_t id,
                            const std::uint64_t context_token,
                            const std::uint32_t object,
                            const VmThreadStatus status,
                            const VmThreadWaitState wait_state,
                            const std::string& name) -> bool {
        const auto interrupted = impl_->vm->Monitors().TryInterrupted(
            context_token);
        if (!interrupted) return false;
        snapshot.push_back({id, context_token, object, status, wait_state,
                            *interrupted, name});
        return true;
    };
    if (impl_->root_object.IsValid() &&
        !append(1U, 1U, impl_->root_object.Value(),
                impl_->shutting_down ? VmThreadStatus::stopped
                                     : VmThreadStatus::running,
                impl_->root_wait_state, impl_->root_name)) {
        return std::nullopt;
    }
    for (const auto& [_, record] : impl_->records) {
        if (!append(record->id, record->context.Token(), record->object.Value(),
                    record->status, record->wait_state, record->name)) {
            return std::nullopt;
        }
    }
    return snapshot;
}

void VmThreadRuntime::VisitThreadRoots(
    const std::function<void(VmObjectRef)>& visitor) const {
    if (!visitor) return;
    const std::lock_guard guard(impl_->mutex);
    visitor(impl_->root_object);
    for (const auto& [_, record] : impl_->records) {
        if (record->status == VmThreadStatus::created ||
            record->status == VmThreadStatus::running) {
            // The Thread object's declared target field is traced by the
            // ordinary object graph; no duplicate runtime edge is needed.
            visitor(record->object);
        }
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
