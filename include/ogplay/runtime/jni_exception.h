#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/jni.h"

namespace ogplay::runtime {

enum class JniExceptionErrorReason : std::uint8_t {
    invalid_thread,
    duplicate_thread,
    invalid_throwable,
    exception_already_pending,
    call_blocked,
};

class JniExceptionError final : public std::runtime_error {
public:
    JniExceptionError(JniExceptionErrorReason reason, std::string message);

    [[nodiscard]] JniExceptionErrorReason Reason() const noexcept {
        return reason_;
    }

private:
    JniExceptionErrorReason reason_;
};

class JniExceptionState final {
public:
    JniExceptionState();
    ~JniExceptionState();
    JniExceptionState(const JniExceptionState&) = delete;
    JniExceptionState& operator=(const JniExceptionState&) = delete;
    JniExceptionState(JniExceptionState&&) noexcept;
    JniExceptionState& operator=(JniExceptionState&&) noexcept;

    void AttachThread(std::uint64_t thread_id);
    void DetachThread(std::uint64_t thread_id);
    [[nodiscard]] bool IsThreadAttached(std::uint64_t thread_id) const;

    void Throw(std::uint64_t thread_id, JniObjectIdentity throwable);
    [[nodiscard]] bool HasPending(std::uint64_t thread_id) const;
    [[nodiscard]] std::optional<JniObjectIdentity> Occurred(
        std::uint64_t thread_id) const;
    void Clear(std::uint64_t thread_id);

    [[nodiscard]] bool IsCallAllowed(std::uint64_t thread_id,
                                     JniSlot slot) const;
    void RequireCallAllowed(std::uint64_t thread_id, JniSlot slot) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
