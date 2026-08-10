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

struct FrameworkLocaleConfig final {
    std::string language{"en"};
    std::string country{"US"};
};

[[nodiscard]] std::int32_t LegacyPhoneLanguageIndex(
    const FrameworkLocaleConfig& config);

enum class FrameworkLocaleErrorReason : std::uint8_t {
    duplicate_install,
    missing_framework,
    invalid_config,
    unknown_locale,
};

class FrameworkLocaleError final : public std::runtime_error {
public:
    FrameworkLocaleError(FrameworkLocaleErrorReason reason,
                         std::string message);
    [[nodiscard]] FrameworkLocaleErrorReason Reason() const noexcept;

private:
    FrameworkLocaleErrorReason reason_;
};

class FrameworkLocaleHle final {
public:
    FrameworkLocaleHle(JniClassRegistry& classes,
                       JniInvocationEngine& invocations,
                       JniEnvironment& environment, JniStringStore& strings,
                       FrameworkLocaleConfig config = {});
    ~FrameworkLocaleHle();
    FrameworkLocaleHle(const FrameworkLocaleHle&) = delete;
    FrameworkLocaleHle& operator=(const FrameworkLocaleHle&) = delete;
    FrameworkLocaleHle(FrameworkLocaleHle&&) noexcept;
    FrameworkLocaleHle& operator=(FrameworkLocaleHle&&) noexcept;

    [[nodiscard]] JniObjectIdentity Install();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
