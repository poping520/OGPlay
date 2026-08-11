#include "ogplay/runtime/jni/jni_environment.h"

#include <condition_variable>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

[[nodiscard]] JniSlot RequiredSlot(const char* name) {
    const auto slot = FindJniSlot(name);
    if (!slot.has_value()) {
        throw std::logic_error("required JNI slot is absent");
    }
    return *slot;
}

}  // namespace

JniMonitorError::JniMonitorError(const JniMonitorErrorReason reason,
                                 std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

JniMonitorErrorReason JniMonitorError::Reason() const noexcept {
    return reason_;
}

class JniMonitorTable::Impl final {
public:
    void Enter(const JniObjectIdentity object,
               const std::uint64_t thread_id) {
        Validate(object, thread_id);
        std::unique_lock lock(mutex_);
        if (shut_down_) ShutDown();
        auto& entry = monitors_[Key(object)];
        if (entry.owner_thread == thread_id) {
            if (entry.recursion == std::numeric_limits<std::size_t>::max()) {
                Fail(JniMonitorErrorReason::recursion_overflow,
                     "JNI monitor recursion count overflowed");
            }
            ++entry.recursion;
            return;
        }
        if (entry.owner_thread != 0U) {
            const auto generation = interrupt_generation_;
            ++entry.waiting_threads;
            entry.changed.wait(lock, [this, &entry, generation] {
                return shut_down_ || interrupt_generation_ != generation ||
                       entry.owner_thread == 0U;
            });
            --entry.waiting_threads;
            if (shut_down_) ShutDown();
            if (interrupt_generation_ != generation) Interrupted();
        }
        entry.owner_thread = thread_id;
        entry.recursion = 1U;
    }

    void Exit(const JniObjectIdentity object,
              const std::uint64_t thread_id) {
        Validate(object, thread_id);
        std::scoped_lock lock(mutex_);
        const auto found = monitors_.find(Key(object));
        if (found == monitors_.end() ||
            found->second.owner_thread != thread_id) {
            Fail(JniMonitorErrorReason::not_owner,
                 "JNI monitor exit requires the owning guest thread");
        }
        if (--found->second.recursion == 0U) {
            found->second.owner_thread = 0U;
            found->second.changed.notify_one();
        }
    }

    [[nodiscard]] std::size_t ReleaseThread(
        const std::uint64_t thread_id) {
        if (thread_id == 0U) {
            Fail(JniMonitorErrorReason::invalid_thread,
                 "JNI monitor thread ID cannot be zero");
        }
        std::scoped_lock lock(mutex_);
        std::size_t released{};
        for (auto& [object, entry] : monitors_) {
            static_cast<void>(object);
            if (entry.owner_thread != thread_id) continue;
            entry.owner_thread = 0U;
            entry.recursion = 0U;
            ++released;
            entry.changed.notify_one();
        }
        return released;
    }

    [[nodiscard]] std::size_t InterruptWaiters() {
        std::scoped_lock lock(mutex_);
        ++interrupt_generation_;
        return WakeAllLocked();
    }

    [[nodiscard]] std::size_t Shutdown() {
        std::scoped_lock lock(mutex_);
        if (shut_down_) return 0U;
        shut_down_ = true;
        return WakeAllLocked();
    }

    [[nodiscard]] JniMonitorSnapshot Snapshot(
        const JniObjectIdentity object) const {
        if (object.value == 0U) {
            Fail(JniMonitorErrorReason::invalid_object,
                 "JNI monitor object identity cannot be zero");
        }
        std::scoped_lock lock(mutex_);
        const auto found = monitors_.find(Key(object));
        if (found == monitors_.end()) {
            return {.interrupt_generation = interrupt_generation_,
                    .shut_down = shut_down_};
        }
        return {found->second.owner_thread, found->second.recursion,
                found->second.waiting_threads, interrupt_generation_,
                shut_down_};
    }

private:
    struct Entry final {
        std::condition_variable changed;
        std::uint64_t owner_thread{};
        std::size_t recursion{};
        std::size_t waiting_threads{};
    };

    using MonitorKey = std::pair<JniObjectDomain, std::uint64_t>;

    [[nodiscard]] std::size_t WakeAllLocked() {
        std::size_t waiting{};
        for (auto& [object, entry] : monitors_) {
            static_cast<void>(object);
            waiting += entry.waiting_threads;
            entry.changed.notify_all();
        }
        return waiting;
    }

    [[nodiscard]] static MonitorKey Key(const JniObjectIdentity object) {
        return {object.domain, object.value};
    }

    static void Validate(const JniObjectIdentity object,
                         const std::uint64_t thread_id) {
        if (thread_id == 0U) {
            Fail(JniMonitorErrorReason::invalid_thread,
                 "JNI monitor thread ID cannot be zero");
        }
        if (object.value == 0U) {
            Fail(JniMonitorErrorReason::invalid_object,
                 "JNI monitor object identity cannot be zero");
        }
    }

    [[noreturn]] static void Interrupted() {
        Fail(JniMonitorErrorReason::interrupted,
             "JNI monitor wait was interrupted");
    }

    [[noreturn]] static void ShutDown() {
        Fail(JniMonitorErrorReason::shut_down,
             "JNI monitor table is shut down");
    }

    [[noreturn]] static void Fail(const JniMonitorErrorReason reason,
                                  const char* message) {
        throw JniMonitorError(reason, message);
    }

    mutable std::mutex mutex_;
    std::map<MonitorKey, Entry> monitors_;
    std::uint64_t interrupt_generation_{};
    bool shut_down_{};
};

JniMonitorTable::JniMonitorTable() : impl_(std::make_unique<Impl>()) {}
JniMonitorTable::~JniMonitorTable() = default;
JniMonitorTable::JniMonitorTable(JniMonitorTable&&) noexcept = default;
JniMonitorTable& JniMonitorTable::operator=(JniMonitorTable&&) noexcept =
    default;
void JniMonitorTable::Enter(const JniObjectIdentity object,
                            const std::uint64_t thread_id) {
    impl_->Enter(object, thread_id);
}
void JniMonitorTable::Exit(const JniObjectIdentity object,
                           const std::uint64_t thread_id) {
    impl_->Exit(object, thread_id);
}
std::size_t JniMonitorTable::ReleaseThread(const std::uint64_t thread_id) {
    return impl_->ReleaseThread(thread_id);
}
std::size_t JniMonitorTable::InterruptWaiters() {
    return impl_->InterruptWaiters();
}
std::size_t JniMonitorTable::Shutdown() {
    return impl_->Shutdown();
}
JniMonitorSnapshot JniMonitorTable::Snapshot(
    const JniObjectIdentity object) const {
    return impl_->Snapshot(object);
}

JniEnvironment::JniEnvironment(const JniReferenceLimits limits)
    : references_(limits) {}

void JniEnvironment::AttachThread(const std::uint64_t thread_id,
                                  const std::size_t initial_local_capacity) {
    exceptions_.AttachThread(thread_id);
    try {
        references_.AttachThread(thread_id, initial_local_capacity);
    } catch (...) {
        exceptions_.DetachThread(thread_id);
        throw;
    }
}

void JniEnvironment::DetachThread(const std::uint64_t thread_id) {
    static_cast<void>(monitors_.ReleaseThread(thread_id));
    references_.DetachThread(thread_id);
    exceptions_.DetachThread(thread_id);
}

bool JniEnvironment::IsThreadAttached(const std::uint64_t thread_id) const {
    return references_.IsThreadAttached(thread_id) &&
           exceptions_.IsThreadAttached(thread_id);
}

JniInt JniEnvironment::GetVersion(const std::uint64_t thread_id) const {
    RequireAllowed(thread_id, "GetVersion");
    return kJniVersion1_6;
}

void JniEnvironment::EnsureLocalCapacity(
    const std::uint64_t thread_id, const std::size_t additional_capacity) {
    RequireAllowed(thread_id, "EnsureLocalCapacity");
    references_.EnsureLocalCapacity(thread_id, additional_capacity);
}

void JniEnvironment::PushLocalFrame(const std::uint64_t thread_id,
                                    const std::size_t capacity) {
    RequireAllowed(thread_id, "PushLocalFrame");
    references_.PushLocalFrame(thread_id, capacity);
}

JniReference JniEnvironment::PopLocalFrame(const std::uint64_t thread_id,
                                            const JniReference result) {
    RequireAllowed(thread_id, "PopLocalFrame");
    return references_.PopLocalFrame(thread_id, result);
}

JniReference JniEnvironment::NewLocalRef(const std::uint64_t thread_id,
                                         const JniReference source) {
    RequireAllowed(thread_id, "NewLocalRef");
    const auto object = references_.Resolve(thread_id, source);
    return object.has_value() ? references_.NewLocal(thread_id, *object)
                              : JniReference{};
}

JniReference JniEnvironment::PublishLocalObject(
    const std::uint64_t thread_id, const JniObjectIdentity object) {
    RequireAllowed(thread_id, "NewLocalRef");
    return references_.NewLocal(thread_id, object);
}

JniReference JniEnvironment::PublishGlobalObjectForHle(
    const std::uint64_t thread_id, const JniObjectIdentity object) {
    RequireAllowed(thread_id, "NewGlobalRef");
    return references_.NewGlobal(object);
}

void JniEnvironment::DeleteLocalRef(const std::uint64_t thread_id,
                                    const JniReference reference) {
    RequireAllowed(thread_id, "DeleteLocalRef");
    references_.DeleteLocal(thread_id, reference);
}

JniReference JniEnvironment::NewGlobalRef(const std::uint64_t thread_id,
                                          const JniReference source) {
    RequireAllowed(thread_id, "NewGlobalRef");
    const auto object = references_.Resolve(thread_id, source);
    return object.has_value() ? references_.NewGlobal(*object) : JniReference{};
}

void JniEnvironment::DeleteGlobalRef(const std::uint64_t thread_id,
                                     const JniReference reference) {
    RequireAllowed(thread_id, "DeleteGlobalRef");
    references_.DeleteGlobal(reference);
}

JniReference JniEnvironment::NewWeakGlobalRef(const std::uint64_t thread_id,
                                              const JniReference source) {
    RequireAllowed(thread_id, "NewWeakGlobalRef");
    const auto object = references_.Resolve(thread_id, source);
    return object.has_value() ? references_.NewWeakGlobal(*object)
                              : JniReference{};
}

void JniEnvironment::DeleteWeakGlobalRef(const std::uint64_t thread_id,
                                         const JniReference reference) {
    RequireAllowed(thread_id, "DeleteWeakGlobalRef");
    references_.DeleteWeakGlobal(reference);
}

bool JniEnvironment::IsSameObject(const std::uint64_t thread_id,
                                  const JniReference left,
                                  const JniReference right) const {
    RequireAllowed(thread_id, "IsSameObject");
    return references_.IsSameObject(thread_id, left, right);
}

std::optional<JniObjectIdentity> JniEnvironment::ResolveObjectForHle(
    const std::uint64_t thread_id, const JniReference reference) const {
    RequireAllowed(thread_id, "GetObjectClass");
    return references_.Resolve(thread_id, reference);
}

void JniEnvironment::Throw(const std::uint64_t thread_id,
                           const JniReference throwable) {
    RequireAllowed(thread_id, "Throw");
    const auto object = references_.Resolve(thread_id, throwable);
    exceptions_.Throw(thread_id, object.value_or(JniObjectIdentity{}));
}

void JniEnvironment::ThrowNew(
    const std::uint64_t thread_id,
    const JniObjectIdentity exception_class,
    std::string modified_utf8_message) {
    RequireAllowed(thread_id, "ThrowNew");
    if (exception_class.value == 0U) {
        throw JniExceptionError(
            JniExceptionErrorReason::invalid_throwable,
            "JNI exception class identity cannot be zero");
    }
    const auto throwable = AllocateJniHostObjectIdentity();
    {
        std::scoped_lock lock(throwable_mutex_);
        throwables_.emplace(
            throwable.value,
            JniThrowableMetadata{throwable, exception_class,
                                 std::move(modified_utf8_message)});
    }
    try {
        exceptions_.Throw(thread_id, throwable);
    } catch (...) {
        std::scoped_lock lock(throwable_mutex_);
        throwables_.erase(throwable.value);
        throw;
    }
}

JniReference JniEnvironment::ExceptionOccurred(const std::uint64_t thread_id) {
    RequireAllowed(thread_id, "ExceptionOccurred");
    const auto throwable = exceptions_.Occurred(thread_id);
    return throwable.has_value() ? references_.NewLocal(thread_id, *throwable)
                                 : JniReference{};
}

bool JniEnvironment::ExceptionCheck(const std::uint64_t thread_id) const {
    RequireAllowed(thread_id, "ExceptionCheck");
    return exceptions_.HasPending(thread_id);
}

void JniEnvironment::ExceptionDescribe(const std::uint64_t thread_id) {
    RequireAllowed(thread_id, "ExceptionDescribe");
    const auto pending = exceptions_.Occurred(thread_id);
    if (!pending.has_value()) return;
    JniThrowableMetadata metadata{*pending, {}, {}};
    {
        std::scoped_lock lock(throwable_mutex_);
        const auto found = throwables_.find(pending->value);
        if (found != throwables_.end()) metadata = found->second;
    }
    exception_logger_.Write(
        core::LogLevel::error, "runtime.jni.exception",
        "pending JNI exception",
        {.guest_thread = thread_id},
        {{"throwable", metadata.throwable.value},
         {"exception_class", metadata.exception_class.value},
         {"message", metadata.modified_utf8_message}},
        {.mode = core::RateLimitMode::none});
}

void JniEnvironment::ExceptionClear(const std::uint64_t thread_id) {
    RequireAllowed(thread_id, "ExceptionClear");
    exceptions_.Clear(thread_id);
}

std::vector<core::LogRecord> JniEnvironment::ExceptionDiagnostics() const {
    return exception_logger_.Snapshot(
        std::nullopt, "runtime.jni.exception");
}

void JniEnvironment::MonitorEnter(const std::uint64_t thread_id,
                                  const JniReference object) {
    RequireAllowed(thread_id, "MonitorEnter");
    const auto identity = references_.Resolve(thread_id, object);
    monitors_.Enter(identity.value_or(JniObjectIdentity{}), thread_id);
}

void JniEnvironment::MonitorExit(const std::uint64_t thread_id,
                                 const JniReference object) {
    RequireAllowed(thread_id, "MonitorExit");
    const auto identity = references_.Resolve(thread_id, object);
    monitors_.Exit(identity.value_or(JniObjectIdentity{}), thread_id);
}

std::size_t JniEnvironment::InterruptMonitorWaiters() {
    return monitors_.InterruptWaiters();
}

std::size_t JniEnvironment::ShutdownMonitors() {
    return monitors_.Shutdown();
}

JniMonitorSnapshot JniEnvironment::MonitorSnapshot(
    const JniObjectIdentity object) const {
    return monitors_.Snapshot(object);
}

void JniEnvironment::RequireAllowed(const std::uint64_t thread_id,
                                    const char* slot_name) const {
    exceptions_.RequireCallAllowed(thread_id, RequiredSlot(slot_name));
}

}  // namespace ogplay::runtime
