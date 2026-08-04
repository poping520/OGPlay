#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {

struct FrameworkPreferencesClassSet final {
    JniObjectIdentity shared_preferences_class;
    JniObjectIdentity editor_class;
};

enum class FrameworkPreferencesErrorReason : std::uint8_t {
    duplicate_install,
    missing_framework,
    invalid_argument,
    unsupported_mode,
    unknown_preferences,
    unknown_editor,
    type_mismatch,
};

class FrameworkPreferencesError final : public std::runtime_error {
public:
    FrameworkPreferencesError(FrameworkPreferencesErrorReason reason,
                              std::string message);
    [[nodiscard]] FrameworkPreferencesErrorReason Reason() const noexcept;

private:
    FrameworkPreferencesErrorReason reason_;
};

class FrameworkPreferencesHle final {
public:
    FrameworkPreferencesHle(JniClassRegistry& classes,
                            JniInvocationEngine& invocations,
                            JniEnvironment& environment,
                            JniStringStore& strings);
    ~FrameworkPreferencesHle();
    FrameworkPreferencesHle(const FrameworkPreferencesHle&) = delete;
    FrameworkPreferencesHle& operator=(const FrameworkPreferencesHle&) =
        delete;
    FrameworkPreferencesHle(FrameworkPreferencesHle&&) noexcept;
    FrameworkPreferencesHle& operator=(FrameworkPreferencesHle&&) noexcept;

    [[nodiscard]] FrameworkPreferencesClassSet Install();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
