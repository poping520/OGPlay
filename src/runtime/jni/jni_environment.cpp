#include "ogplay/runtime/jni/jni_environment.h"

#include <stdexcept>

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

void JniEnvironment::ExceptionClear(const std::uint64_t thread_id) {
    RequireAllowed(thread_id, "ExceptionClear");
    exceptions_.Clear(thread_id);
}

void JniEnvironment::RequireAllowed(const std::uint64_t thread_id,
                                    const char* slot_name) const {
    exceptions_.RequireCallAllowed(thread_id, RequiredSlot(slot_name));
}

}  // namespace ogplay::runtime
