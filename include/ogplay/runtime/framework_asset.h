#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "ogplay/runtime/jni_array.h"
#include "ogplay/runtime/jni_class_registry.h"
#include "ogplay/runtime/jni_environment.h"
#include "ogplay/runtime/jni_invocation.h"
#include "ogplay/runtime/jni_object.h"
#include "ogplay/runtime/vfs.h"

namespace ogplay::runtime {

struct FrameworkAssetClassSet final {
    JniObjectIdentity asset_manager_class;
    JniObjectIdentity input_stream_class;
};

enum class FrameworkAssetErrorReason : std::uint8_t {
    duplicate_install,
    missing_framework,
    invalid_argument,
    invalid_asset_path,
    unknown_stream,
    closed_stream,
    io_failure,
};

class FrameworkAssetError final : public std::runtime_error {
public:
    FrameworkAssetError(FrameworkAssetErrorReason reason,
                        std::string message);
    [[nodiscard]] FrameworkAssetErrorReason Reason() const noexcept;

private:
    FrameworkAssetErrorReason reason_;
};

class FrameworkAssetHle final {
public:
    FrameworkAssetHle(JniClassRegistry& classes,
                      JniInvocationEngine& invocations,
                      JniEnvironment& environment, JniStringStore& strings,
                      JniPrimitiveArrayStore& arrays,
                      VirtualFileSystem& vfs);
    ~FrameworkAssetHle();
    FrameworkAssetHle(const FrameworkAssetHle&) = delete;
    FrameworkAssetHle& operator=(const FrameworkAssetHle&) = delete;
    FrameworkAssetHle(FrameworkAssetHle&&) noexcept;
    FrameworkAssetHle& operator=(FrameworkAssetHle&&) noexcept;

    [[nodiscard]] FrameworkAssetClassSet Install();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
