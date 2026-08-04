#pragma once

#include <cstddef>
#include <cstdint>

#include "ogplay/runtime/jni.h"
#include "ogplay/runtime/jni_exception.h"

namespace ogplay::runtime {

class JniEnvironment final {
public:
    explicit JniEnvironment(JniReferenceLimits limits = {});

    void AttachThread(std::uint64_t thread_id,
                      std::size_t initial_local_capacity = 16);
    void DetachThread(std::uint64_t thread_id);
    [[nodiscard]] bool IsThreadAttached(std::uint64_t thread_id) const;

    [[nodiscard]] JniInt GetVersion(std::uint64_t thread_id) const;
    void EnsureLocalCapacity(std::uint64_t thread_id,
                             std::size_t additional_capacity);
    void PushLocalFrame(std::uint64_t thread_id, std::size_t capacity);
    [[nodiscard]] JniReference PopLocalFrame(
        std::uint64_t thread_id, JniReference result = JniReference{});

    [[nodiscard]] JniReference NewLocalRef(std::uint64_t thread_id,
                                           JniReference source);
    [[nodiscard]] JniReference PublishLocalObject(
        std::uint64_t thread_id, JniObjectIdentity object);
    [[nodiscard]] JniReference PublishGlobalObjectForHle(
        std::uint64_t thread_id, JniObjectIdentity object);
    void DeleteLocalRef(std::uint64_t thread_id, JniReference reference);
    [[nodiscard]] JniReference NewGlobalRef(std::uint64_t thread_id,
                                            JniReference source);
    void DeleteGlobalRef(std::uint64_t thread_id, JniReference reference);
    [[nodiscard]] JniReference NewWeakGlobalRef(std::uint64_t thread_id,
                                                JniReference source);
    void DeleteWeakGlobalRef(std::uint64_t thread_id,
                             JniReference reference);
    [[nodiscard]] bool IsSameObject(std::uint64_t thread_id,
                                    JniReference left,
                                    JniReference right) const;
    [[nodiscard]] std::optional<JniObjectIdentity> ResolveObjectForHle(
        std::uint64_t thread_id, JniReference reference) const;

    void Throw(std::uint64_t thread_id, JniReference throwable);
    [[nodiscard]] JniReference ExceptionOccurred(std::uint64_t thread_id);
    [[nodiscard]] bool ExceptionCheck(std::uint64_t thread_id) const;
    void ExceptionClear(std::uint64_t thread_id);

private:
    void RequireAllowed(std::uint64_t thread_id, const char* slot_name) const;

    JniReferenceTable references_;
    JniExceptionState exceptions_;
};

}  // namespace ogplay::runtime
