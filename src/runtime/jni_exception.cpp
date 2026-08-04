#include "ogplay/runtime/jni_exception.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace ogplay::runtime {
namespace {

constexpr std::array<std::string_view, 24> kPendingExceptionAllowedCalls{
    "ExceptionOccurred",
    "ExceptionDescribe",
    "ExceptionClear",
    "FatalError",
    "DeleteGlobalRef",
    "DeleteLocalRef",
    "DeleteWeakGlobalRef",
    "ReleaseStringChars",
    "ReleaseStringUTFChars",
    "ReleaseBooleanArrayElements",
    "ReleaseByteArrayElements",
    "ReleaseCharArrayElements",
    "ReleaseShortArrayElements",
    "ReleaseIntArrayElements",
    "ReleaseLongArrayElements",
    "ReleaseFloatArrayElements",
    "ReleaseDoubleArrayElements",
    "MonitorExit",
    "ReleasePrimitiveArrayCritical",
    "ReleaseStringCritical",
    "ExceptionCheck",
    "GetObjectRefType",
    "GetJavaVM",
    "PopLocalFrame",
};

[[nodiscard]] bool AllowedWithPendingException(const JniSlot slot) {
    const auto name = JniSlotName(slot);
    return std::find(kPendingExceptionAllowedCalls.begin(),
                     kPendingExceptionAllowedCalls.end(), name) !=
           kPendingExceptionAllowedCalls.end();
}

}  // namespace

JniExceptionError::JniExceptionError(const JniExceptionErrorReason reason,
                                     std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

class JniExceptionState::Impl final {
public:
    void AttachThread(const std::uint64_t thread_id) {
        if (thread_id == 0) InvalidThread();
        std::scoped_lock lock(mutex_);
        if (!threads_.emplace(thread_id, std::nullopt).second) {
            ThrowError(JniExceptionErrorReason::duplicate_thread,
                       "JNI exception thread is already attached");
        }
    }

    void DetachThread(const std::uint64_t thread_id) {
        std::scoped_lock lock(mutex_);
        if (threads_.erase(thread_id) == 0) InvalidThread();
    }

    [[nodiscard]] bool IsThreadAttached(const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        return threads_.contains(thread_id);
    }

    void SetPending(const std::uint64_t thread_id,
                    const JniObjectIdentity throwable) {
        if (throwable.value == 0) {
            ThrowError(JniExceptionErrorReason::invalid_throwable,
                       "pending JNI throwable identity cannot be zero");
        }
        std::scoped_lock lock(mutex_);
        auto& pending = Thread(thread_id);
        if (pending.has_value()) {
            ThrowError(JniExceptionErrorReason::exception_already_pending,
                       "JNI thread already has a pending exception");
        }
        pending = throwable;
    }

    [[nodiscard]] bool HasPending(const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        return Thread(thread_id).has_value();
    }

    [[nodiscard]] std::optional<JniObjectIdentity> Occurred(
        const std::uint64_t thread_id) const {
        std::scoped_lock lock(mutex_);
        return Thread(thread_id);
    }

    void Clear(const std::uint64_t thread_id) {
        std::scoped_lock lock(mutex_);
        Thread(thread_id).reset();
    }

    [[nodiscard]] bool IsCallAllowed(const std::uint64_t thread_id,
                                     const JniSlot slot) const {
        std::scoped_lock lock(mutex_);
        const auto& pending = Thread(thread_id);
        return !pending.has_value() || AllowedWithPendingException(slot);
    }

    void RequireCallAllowed(const std::uint64_t thread_id,
                            const JniSlot slot) const {
        if (IsCallAllowed(thread_id, slot)) return;
        ThrowError(JniExceptionErrorReason::call_blocked,
                   "JNI call is blocked by a pending exception: " +
                       std::string(JniSlotName(slot)));
    }

private:
    using Pending = std::optional<JniObjectIdentity>;

    [[noreturn]] static void ThrowError(const JniExceptionErrorReason reason,
                                        std::string message) {
        throw JniExceptionError(reason, std::move(message));
    }

    [[noreturn]] static void InvalidThread() {
        ThrowError(JniExceptionErrorReason::invalid_thread,
                   "JNI exception thread is not attached");
    }

    [[nodiscard]] Pending& Thread(const std::uint64_t thread_id) {
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) InvalidThread();
        return found->second;
    }

    [[nodiscard]] const Pending& Thread(const std::uint64_t thread_id) const {
        const auto found = threads_.find(thread_id);
        if (found == threads_.end()) InvalidThread();
        return found->second;
    }

    mutable std::mutex mutex_;
    std::map<std::uint64_t, Pending> threads_;
};

JniExceptionState::JniExceptionState() : impl_(std::make_unique<Impl>()) {}
JniExceptionState::~JniExceptionState() = default;
JniExceptionState::JniExceptionState(JniExceptionState&&) noexcept = default;
JniExceptionState& JniExceptionState::operator=(JniExceptionState&&) noexcept = default;

void JniExceptionState::AttachThread(const std::uint64_t thread_id) {
    impl_->AttachThread(thread_id);
}

void JniExceptionState::DetachThread(const std::uint64_t thread_id) {
    impl_->DetachThread(thread_id);
}

bool JniExceptionState::IsThreadAttached(const std::uint64_t thread_id) const {
    return impl_->IsThreadAttached(thread_id);
}

void JniExceptionState::Throw(const std::uint64_t thread_id,
                              const JniObjectIdentity throwable) {
    impl_->SetPending(thread_id, throwable);
}

bool JniExceptionState::HasPending(const std::uint64_t thread_id) const {
    return impl_->HasPending(thread_id);
}

std::optional<JniObjectIdentity> JniExceptionState::Occurred(
    const std::uint64_t thread_id) const {
    return impl_->Occurred(thread_id);
}

void JniExceptionState::Clear(const std::uint64_t thread_id) {
    impl_->Clear(thread_id);
}

bool JniExceptionState::IsCallAllowed(const std::uint64_t thread_id,
                                      const JniSlot slot) const {
    return impl_->IsCallAllowed(thread_id, slot);
}

void JniExceptionState::RequireCallAllowed(const std::uint64_t thread_id,
                                           const JniSlot slot) const {
    impl_->RequireCallAllowed(thread_id, slot);
}

}  // namespace ogplay::runtime
