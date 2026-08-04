#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/jni_class_registry.h"
#include "ogplay/runtime/jni_invocation.h"

namespace ogplay::runtime {

enum class FrameworkActivityState : std::uint8_t {
    constructed,
    created,
    started,
    resumed,
    paused,
    stopped,
    destroyed,
};

struct FrameworkLifecycleEvent final {
    std::uint64_t sequence{};
    std::uint64_t thread_id{};
    JniReference receiver;
    std::string callback;
    FrameworkActivityState before{FrameworkActivityState::constructed};
    FrameworkActivityState after{FrameworkActivityState::constructed};
};

struct FrameworkClassSet final {
    JniObjectIdentity object_class;
    JniObjectIdentity context_class;
    JniObjectIdentity context_wrapper_class;
    JniObjectIdentity bundle_class;
    JniObjectIdentity activity_class;
};

enum class FrameworkLifecycleErrorReason : std::uint8_t {
    duplicate_install,
    duplicate_instance,
    unknown_instance,
    invalid_transition,
};

class FrameworkLifecycleError final : public std::runtime_error {
public:
    FrameworkLifecycleError(FrameworkLifecycleErrorReason reason,
                            std::string message);
    [[nodiscard]] FrameworkLifecycleErrorReason Reason() const noexcept;

private:
    FrameworkLifecycleErrorReason reason_;
};

class FrameworkLifecycleHle final {
public:
    FrameworkLifecycleHle(JniClassRegistry& classes,
                          JniInvocationEngine& invocations);
    ~FrameworkLifecycleHle();
    FrameworkLifecycleHle(const FrameworkLifecycleHle&) = delete;
    FrameworkLifecycleHle& operator=(const FrameworkLifecycleHle&) = delete;
    FrameworkLifecycleHle(FrameworkLifecycleHle&&) noexcept;
    FrameworkLifecycleHle& operator=(FrameworkLifecycleHle&&) noexcept;

    [[nodiscard]] FrameworkClassSet Install();
    [[nodiscard]] FrameworkActivityState State(JniReference receiver) const;
    [[nodiscard]] std::vector<FrameworkLifecycleEvent> Events() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
