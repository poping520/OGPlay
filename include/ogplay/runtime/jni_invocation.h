#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>

#include "ogplay/runtime/jni_class_registry.h"

namespace ogplay::runtime {

using JniValue =
    std::variant<std::monostate, JniBoolean, JniByte, JniChar, JniShort,
                 JniInt, JniLong, JniFloat, JniDouble, JniReference>;

enum class JniArgumentSource : std::uint8_t {
    variadic,
    va_list,
    value_array,
};

enum class JniInvocationKind : std::uint8_t {
    virtual_instance,
    nonvirtual_instance,
    static_method,
};

struct JniInvocation final {
    std::uint64_t thread_id{};
    JniInvocationKind kind{JniInvocationKind::virtual_instance};
    JniArgumentSource argument_source{JniArgumentSource::value_array};
    JniReference receiver;
    JniObjectIdentity dispatch_class;
    JniResolvedMethod method;
    std::span<const JniValue> arguments;
};

using JniMethodHandler = std::function<JniValue(const JniInvocation&)>;

enum class JniInvocationErrorReason : std::uint8_t {
    invalid_thread,
    invalid_receiver,
    wrong_method_kind,
    incompatible_class,
    argument_count_mismatch,
    argument_type_mismatch,
    return_type_mismatch,
    duplicate_handler,
    missing_handler,
};

class JniInvocationError final : public std::runtime_error {
public:
    JniInvocationError(JniInvocationErrorReason reason, std::string message);
    [[nodiscard]] JniInvocationErrorReason Reason() const noexcept;

private:
    JniInvocationErrorReason reason_;
};

class JniInvocationEngine final {
public:
    explicit JniInvocationEngine(const JniClassRegistry& classes);
    ~JniInvocationEngine();
    JniInvocationEngine(const JniInvocationEngine&) = delete;
    JniInvocationEngine& operator=(const JniInvocationEngine&) = delete;
    JniInvocationEngine(JniInvocationEngine&&) noexcept;
    JniInvocationEngine& operator=(JniInvocationEngine&&) noexcept;

    void RegisterHandler(std::string implementation, JniMethodHandler handler);

    [[nodiscard]] JniValue InvokeVirtual(
        std::uint64_t thread_id, JniReference receiver,
        JniObjectIdentity receiver_class, JniMethodId method,
        std::span<const JniValue> arguments, JniArgumentSource source) const;
    [[nodiscard]] JniValue InvokeNonvirtual(
        std::uint64_t thread_id, JniReference receiver,
        JniObjectIdentity receiver_class, JniObjectIdentity dispatch_class,
        JniMethodId method, std::span<const JniValue> arguments,
        JniArgumentSource source) const;
    [[nodiscard]] JniValue InvokeStatic(
        std::uint64_t thread_id, JniObjectIdentity dispatch_class,
        JniMethodId method, std::span<const JniValue> arguments,
        JniArgumentSource source) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
