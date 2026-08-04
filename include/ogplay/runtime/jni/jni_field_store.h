#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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

class JniFieldStore final {
public:
    explicit JniFieldStore(const JniClassRegistry& classes);
    ~JniFieldStore();
    JniFieldStore(const JniFieldStore&) = delete;
    JniFieldStore& operator=(const JniFieldStore&) = delete;
    JniFieldStore(JniFieldStore&&) noexcept;
    JniFieldStore& operator=(JniFieldStore&&) noexcept;

    [[nodiscard]] JniValue GetInstance(JniObjectIdentity object,
                                       JniObjectIdentity object_class,
                                       JniFieldId field) const;
    void SetInstance(JniObjectIdentity object, JniObjectIdentity object_class,
                     JniFieldId field, JniValue value);
    void DeleteInstanceFields(JniObjectIdentity object);

    [[nodiscard]] JniValue GetStatic(JniObjectIdentity dispatch_class,
                                     JniFieldId field) const;
    void SetStatic(JniObjectIdentity dispatch_class, JniFieldId field,
                   JniValue value);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
