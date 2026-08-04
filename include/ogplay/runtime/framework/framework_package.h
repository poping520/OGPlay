#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_field_store.h"
#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {

struct FrameworkPackageConfig final {
    std::string package_name;
    std::u16string version_name;
    JniInt version_code{};
};

struct FrameworkPackageClassSet final {
    JniObjectIdentity package_manager_class;
    JniObjectIdentity package_info_class;
};

enum class FrameworkPackageErrorReason : std::uint8_t {
    duplicate_install,
    missing_framework,
    invalid_config,
    invalid_argument,
    unknown_manager,
    unknown_package,
    unsupported_flags,
};

class FrameworkPackageError final : public std::runtime_error {
public:
    FrameworkPackageError(FrameworkPackageErrorReason reason,
                          std::string message);
    [[nodiscard]] FrameworkPackageErrorReason Reason() const noexcept;

private:
    FrameworkPackageErrorReason reason_;
};

class FrameworkPackageHle final {
public:
    FrameworkPackageHle(JniClassRegistry& classes,
                        JniInvocationEngine& invocations,
                        JniEnvironment& environment, JniStringStore& strings,
                        JniFieldStore& fields,
                        FrameworkPackageConfig config);
    ~FrameworkPackageHle();
    FrameworkPackageHle(const FrameworkPackageHle&) = delete;
    FrameworkPackageHle& operator=(const FrameworkPackageHle&) = delete;
    FrameworkPackageHle(FrameworkPackageHle&&) noexcept;
    FrameworkPackageHle& operator=(FrameworkPackageHle&&) noexcept;

    [[nodiscard]] FrameworkPackageClassSet Install();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
