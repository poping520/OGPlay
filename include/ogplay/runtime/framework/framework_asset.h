#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/runtime/jni/jni_array.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/jni/jni_invocation.h"
#include "ogplay/runtime/jni/jni_object.h"
#include "ogplay/runtime/vfs/vfs.h"

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

struct FrameworkDirectAssetImplementations final {
    std::string load_full;
    std::string load_range;
    std::string length;
};

class FrameworkDirectAssetHle final {
public:
    FrameworkDirectAssetHle(JniInvocationEngine& invocations,
                            JniEnvironment& environment,
                            JniStringStore& strings,
                            JniPrimitiveArrayStore& arrays,
                            VirtualFileSystem& vfs);
    ~FrameworkDirectAssetHle();
    FrameworkDirectAssetHle(const FrameworkDirectAssetHle&) = delete;
    FrameworkDirectAssetHle& operator=(const FrameworkDirectAssetHle&) = delete;
    FrameworkDirectAssetHle(FrameworkDirectAssetHle&&) noexcept;
    FrameworkDirectAssetHle& operator=(FrameworkDirectAssetHle&&) noexcept;

    void Install(FrameworkDirectAssetImplementations implementations);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
