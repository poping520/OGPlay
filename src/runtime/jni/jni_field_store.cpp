#include "ogplay/runtime/jni/jni_field_store.h"

#include <map>
#include <mutex>
#include <tuple>
#include <utility>

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniFieldStoreErrorReason reason,
                       const char* message) {
    throw JniFieldStoreError(reason, message);
}

[[nodiscard]] bool Matches(const JniTypeDescriptor& type,
                           const JniValue& value) {
    switch (type.kind) {
    case JniTypeKind::boolean:
        return std::holds_alternative<JniBoolean>(value);
    case JniTypeKind::byte:
        return std::holds_alternative<JniByte>(value);
    case JniTypeKind::character:
        return std::holds_alternative<JniChar>(value);
    case JniTypeKind::short_integer:
        return std::holds_alternative<JniShort>(value);
    case JniTypeKind::integer:
        return std::holds_alternative<JniInt>(value);
    case JniTypeKind::long_integer:
        return std::holds_alternative<JniLong>(value);
    case JniTypeKind::float_value:
        return std::holds_alternative<JniFloat>(value);
    case JniTypeKind::double_value:
        return std::holds_alternative<JniDouble>(value);
    case JniTypeKind::object:
    case JniTypeKind::array:
        return std::holds_alternative<JniReference>(value);
    case JniTypeKind::void_value:
        return false;
    }
    return false;
}

[[nodiscard]] JniValue DefaultValue(const JniTypeDescriptor& type) {
    switch (type.kind) {
    case JniTypeKind::boolean:
        return JniBoolean{};
    case JniTypeKind::byte:
        return JniByte{};
    case JniTypeKind::character:
        return JniChar{};
    case JniTypeKind::short_integer:
        return JniShort{};
    case JniTypeKind::integer:
        return JniInt{};
    case JniTypeKind::long_integer:
        return JniLong{};
    case JniTypeKind::float_value:
        return JniFloat{};
    case JniTypeKind::double_value:
        return JniDouble{};
    case JniTypeKind::object:
    case JniTypeKind::array:
        return JniReference{};
    case JniTypeKind::void_value:
        Fail(JniFieldStoreErrorReason::unknown_field,
             "JNI field cannot have void type");
    }
    Fail(JniFieldStoreErrorReason::unknown_field,
         "JNI field has an unknown type");
}

}  // namespace

JniFieldStoreError::JniFieldStoreError(const JniFieldStoreErrorReason reason,
                                       const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniFieldStoreErrorReason JniFieldStoreError::Reason() const noexcept {
    return reason_;
}

class JniFieldStore::Impl final {
public:
    explicit Impl(const JniClassRegistry& classes) : classes_(&classes) {}

    [[nodiscard]] JniValue GetInstance(
        const JniObjectIdentity object, const JniObjectIdentity object_class,
        const JniFieldId field) const {
        const auto resolved = ValidateInstance(object, object_class, field);
        std::scoped_lock lock(mutex_);
        const auto found = instance_values_.find(InstanceKey(object, field));
        return found == instance_values_.end() ? DefaultValue(resolved.type)
                                               : found->second;
    }

    void SetInstance(const JniObjectIdentity object,
                     const JniObjectIdentity object_class,
                     const JniFieldId field, JniValue value) {
        const auto resolved = ValidateInstance(object, object_class, field);
        ValidateValue(resolved.type, value);
        std::scoped_lock lock(mutex_);
        instance_values_.insert_or_assign(InstanceKey(object, field),
                                          std::move(value));
    }

    void DeleteInstanceFields(const JniObjectIdentity object) {
        if (object.value == 0) {
            Fail(JniFieldStoreErrorReason::invalid_object,
                 "JNI field object identity is null");
        }
        std::scoped_lock lock(mutex_);
        for (auto iterator = instance_values_.begin();
             iterator != instance_values_.end();) {
            if (std::get<0>(iterator->first) == object.domain &&
                std::get<1>(iterator->first) == object.value) {
                iterator = instance_values_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    [[nodiscard]] JniValue GetStatic(
        const JniObjectIdentity dispatch_class,
        const JniFieldId field) const {
        const auto resolved = ValidateStatic(dispatch_class, field);
        std::scoped_lock lock(mutex_);
        const auto found = static_values_.find(field.Value());
        return found == static_values_.end() ? DefaultValue(resolved.type)
                                             : found->second;
    }

    void SetStatic(const JniObjectIdentity dispatch_class,
                   const JniFieldId field, JniValue value) {
        const auto resolved = ValidateStatic(dispatch_class, field);
        ValidateValue(resolved.type, value);
        std::scoped_lock lock(mutex_);
        static_values_.insert_or_assign(field.Value(), std::move(value));
    }

private:
    using Key = std::tuple<JniObjectDomain, std::uint64_t, std::uint32_t>;

    [[nodiscard]] static Key InstanceKey(const JniObjectIdentity object,
                                         const JniFieldId field) {
        return {object.domain, object.value, field.Value()};
    }

    [[nodiscard]] JniResolvedField Resolve(const JniFieldId field) const {
        try {
            return classes_->ResolveField(field);
        } catch (const JniClassRegistryError&) {
            Fail(JniFieldStoreErrorReason::unknown_field,
                 "JNI field ID is unknown");
        }
    }

    void ValidateAssignable(const JniObjectIdentity target,
                            const JniObjectIdentity source) const {
        try {
            if (!classes_->IsAssignableFrom(target, source)) {
                Fail(JniFieldStoreErrorReason::incompatible_class,
                     "JNI field class is not assignable to declaration");
            }
        } catch (const JniClassRegistryError&) {
            Fail(JniFieldStoreErrorReason::incompatible_class,
                 "JNI field class is not registered");
        }
    }

    [[nodiscard]] JniResolvedField ValidateInstance(
        const JniObjectIdentity object, const JniObjectIdentity object_class,
        const JniFieldId field) const {
        if (object.value == 0) {
            Fail(JniFieldStoreErrorReason::invalid_object,
                 "JNI field object identity is null");
        }
        const auto resolved = Resolve(field);
        if (resolved.declaration.is_static) {
            Fail(JniFieldStoreErrorReason::wrong_field_kind,
                 "static JNI field used as instance field");
        }
        ValidateAssignable(resolved.declaring_class, object_class);
        return resolved;
    }

    [[nodiscard]] JniResolvedField ValidateStatic(
        const JniObjectIdentity dispatch_class,
        const JniFieldId field) const {
        const auto resolved = Resolve(field);
        if (!resolved.declaration.is_static) {
            Fail(JniFieldStoreErrorReason::wrong_field_kind,
                 "instance JNI field used as static field");
        }
        ValidateAssignable(resolved.declaring_class, dispatch_class);
        return resolved;
    }

    static void ValidateValue(const JniTypeDescriptor& type,
                              const JniValue& value) {
        if (!Matches(type, value)) {
            Fail(JniFieldStoreErrorReason::type_mismatch,
                 "JNI field value does not match its descriptor");
        }
    }

    const JniClassRegistry* classes_{};
    mutable std::mutex mutex_;
    std::map<Key, JniValue> instance_values_;
    std::map<std::uint32_t, JniValue> static_values_;
};

JniFieldStore::JniFieldStore(const JniClassRegistry& classes)
    : impl_(std::make_unique<Impl>(classes)) {}
JniFieldStore::~JniFieldStore() = default;
JniFieldStore::JniFieldStore(JniFieldStore&&) noexcept = default;
JniFieldStore& JniFieldStore::operator=(JniFieldStore&&) noexcept = default;

JniValue JniFieldStore::GetInstance(const JniObjectIdentity object,
                                    const JniObjectIdentity object_class,
                                    const JniFieldId field) const {
    return impl_->GetInstance(object, object_class, field);
}
void JniFieldStore::SetInstance(const JniObjectIdentity object,
                                const JniObjectIdentity object_class,
                                const JniFieldId field, JniValue value) {
    impl_->SetInstance(object, object_class, field, std::move(value));
}
void JniFieldStore::DeleteInstanceFields(const JniObjectIdentity object) {
    impl_->DeleteInstanceFields(object);
}
JniValue JniFieldStore::GetStatic(const JniObjectIdentity dispatch_class,
                                  const JniFieldId field) const {
    return impl_->GetStatic(dispatch_class, field);
}
void JniFieldStore::SetStatic(const JniObjectIdentity dispatch_class,
                              const JniFieldId field, JniValue value) {
    impl_->SetStatic(dispatch_class, field, std::move(value));
}

}  // namespace ogplay::runtime
