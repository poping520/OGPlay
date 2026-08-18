#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_exception.h"

namespace ogplay::runtime {

enum class JniMonitorErrorReason : std::uint8_t {
    invalid_thread,
    invalid_object,
    recursion_overflow,
    not_owner,
    interrupted,
    shut_down,
};

class JniMonitorError final : public std::runtime_error {
public:
    JniMonitorError(JniMonitorErrorReason reason, std::string message);
    [[nodiscard]] JniMonitorErrorReason Reason() const noexcept;

private:
    JniMonitorErrorReason reason_;
};

struct JniMonitorSnapshot final {
    std::uint64_t owner_thread{};
    std::size_t recursion{};
    std::size_t waiting_threads{};
    std::uint64_t interrupt_generation{};
    bool shut_down{};
};

class JniMonitorTable final {
public:
    JniMonitorTable();
    ~JniMonitorTable();
    JniMonitorTable(const JniMonitorTable&) = delete;
    JniMonitorTable& operator=(const JniMonitorTable&) = delete;
    JniMonitorTable(JniMonitorTable&&) noexcept;
    JniMonitorTable& operator=(JniMonitorTable&&) noexcept;

    void Enter(JniObjectIdentity object, std::uint64_t thread_id);
    void Exit(JniObjectIdentity object, std::uint64_t thread_id);
    [[nodiscard]] std::size_t ReleaseThread(std::uint64_t thread_id);
    [[nodiscard]] std::size_t InterruptWaiters();
    [[nodiscard]] std::size_t Shutdown();
    [[nodiscard]] JniMonitorSnapshot Snapshot(
        JniObjectIdentity object) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct JniThrowableMetadata final {
    JniObjectIdentity throwable;
    JniObjectIdentity exception_class;
    std::string modified_utf8_message;

    bool operator==(const JniThrowableMetadata&) const = default;
};

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
    [[nodiscard]] std::size_t GlobalReferenceCount() const;

    void Throw(std::uint64_t thread_id, JniReference throwable);
    void ThrowNew(std::uint64_t thread_id,
                  JniObjectIdentity exception_class,
                  std::string modified_utf8_message);
    [[nodiscard]] JniReference ExceptionOccurred(std::uint64_t thread_id);
    [[nodiscard]] bool ExceptionCheck(std::uint64_t thread_id) const;
    void ExceptionDescribe(std::uint64_t thread_id);
    void ExceptionClear(std::uint64_t thread_id);
    [[nodiscard]] std::vector<core::LogRecord> ExceptionDiagnostics() const;
    void MonitorEnter(std::uint64_t thread_id, JniReference object);
    void MonitorExit(std::uint64_t thread_id, JniReference object);
    [[nodiscard]] std::size_t InterruptMonitorWaiters();
    [[nodiscard]] std::size_t ShutdownMonitors();
    [[nodiscard]] JniMonitorSnapshot MonitorSnapshot(
        JniObjectIdentity object) const;

private:
    void RequireAllowed(std::uint64_t thread_id, const char* slot_name) const;

    JniReferenceTable references_;
    JniExceptionState exceptions_;
    JniMonitorTable monitors_;
    mutable std::mutex throwable_mutex_;
    std::map<std::uint64_t, JniThrowableMetadata> throwables_;
    core::Logger exception_logger_;
};

}  // namespace ogplay::runtime
