#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include "ogplay/runtime/jni/jni_invocation.h"

namespace ogplay::runtime {

enum class JniFieldStoreErrorReason : std::uint8_t {
    unknown_field,
    wrong_field_kind,
    invalid_object,
    incompatible_class,
    type_mismatch,
};

class JniFieldStoreError final : public std::runtime_error {
public:
    JniFieldStoreError(JniFieldStoreErrorReason reason, const char* message);
    [[nodiscard]] JniFieldStoreErrorReason Reason() const noexcept;

private:
    JniFieldStoreErrorReason reason_;
};

// Optional owner bridge for fields whose storage lives outside JniFieldStore.
// A nullopt/false result means "not owned" and preserves the platform HLE
// side-table path. The thread id is required for JNI local-reference semantics.
struct JniFieldAccessHooks final {
    std::function<std::optional<bool>(JniObjectIdentity, std::uint64_t)>
        ensure_class_initialized;
    std::function<std::optional<JniValue>(
        JniObjectIdentity, JniObjectIdentity, const JniResolvedField&,
        std::uint64_t)> get_instance;
    std::function<bool(JniObjectIdentity, JniObjectIdentity,
                       const JniResolvedField&, const JniValue&,
                       std::uint64_t)> set_instance;
    std::function<std::optional<JniValue>(
        JniObjectIdentity, const JniResolvedField&, std::uint64_t)> get_static;
    std::function<bool(JniObjectIdentity, const JniResolvedField&,
                       const JniValue&, std::uint64_t)> set_static;
};

class JniFieldStore final {
public:
    explicit JniFieldStore(const JniClassRegistry& classes);
    ~JniFieldStore();
    JniFieldStore(const JniFieldStore&) = delete;
    JniFieldStore& operator=(const JniFieldStore&) = delete;
    JniFieldStore(JniFieldStore&&) noexcept;
    JniFieldStore& operator=(JniFieldStore&&) noexcept;

    void SetAccessHooks(JniFieldAccessHooks hooks);
    [[nodiscard]] bool EnsureClassInitialized(JniObjectIdentity java_class,
                                              std::uint64_t thread_id) const;

    [[nodiscard]] JniValue GetInstance(JniObjectIdentity object,
                                       JniObjectIdentity object_class,
                                       JniFieldId field,
                                       std::uint64_t thread_id = 0) const;
    void SetInstance(JniObjectIdentity object, JniObjectIdentity object_class,
                     JniFieldId field, JniValue value,
                     std::uint64_t thread_id = 0);
    void DeleteInstanceFields(JniObjectIdentity object);

    [[nodiscard]] JniValue GetStatic(JniObjectIdentity dispatch_class,
                                     JniFieldId field,
                                     std::uint64_t thread_id = 0) const;
    void SetStatic(JniObjectIdentity dispatch_class, JniFieldId field,
                   JniValue value, std::uint64_t thread_id = 0);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
