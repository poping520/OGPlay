#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/memory/address.h"
#include "ogplay/runtime/jni/jni.h"

namespace ogplay::runtime {

struct JniNativeMethod final {
    std::string name;
    std::string descriptor;
    memory::GuestAddress target;
};

enum class JniNativeRegistryErrorReason : std::uint8_t {
    invalid_class,
    invalid_name,
    invalid_descriptor,
    invalid_target,
    duplicate_declaration,
    conflicting_declaration,
};

class JniNativeRegistryError final : public std::runtime_error {
public:
    JniNativeRegistryError(JniNativeRegistryErrorReason reason,
                           std::string message);
    [[nodiscard]] JniNativeRegistryErrorReason Reason() const noexcept;

private:
    JniNativeRegistryErrorReason reason_;
};

class JniNativeRegistry final {
public:
    JniNativeRegistry();
    ~JniNativeRegistry();
    JniNativeRegistry(const JniNativeRegistry&) = delete;
    JniNativeRegistry& operator=(const JniNativeRegistry&) = delete;
    JniNativeRegistry(JniNativeRegistry&&) noexcept;
    JniNativeRegistry& operator=(JniNativeRegistry&&) noexcept;

    void RegisterNatives(JniObjectIdentity java_class,
                         std::span<const JniNativeMethod> methods);
    [[nodiscard]] std::size_t UnregisterNatives(
        JniObjectIdentity java_class);
    [[nodiscard]] std::optional<memory::GuestAddress> Resolve(
        JniObjectIdentity java_class, const std::string& name,
        const std::string& descriptor) const;
    [[nodiscard]] std::vector<JniNativeMethod> Declarations(
        JniObjectIdentity java_class) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
