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

    void SetAccessHooks(JniFieldAccessHooks hooks) {
        std::scoped_lock lock(hooks_mutex_);
        hooks_ = std::move(hooks);
    }

    [[nodiscard]] bool EnsureClassInitialized(
        const JniObjectIdentity java_class,
        const std::uint64_t thread_id) const {
        const auto hooks = Hooks();
        if (!hooks.ensure_class_initialized) return true;
        const auto result = hooks.ensure_class_initialized(java_class,
                                                            thread_id);
        return !result.has_value() || *result;
    }

    [[nodiscard]] JniValue GetInstance(
        const JniObjectIdentity object, const JniObjectIdentity object_class,
        const JniFieldId field, const std::uint64_t thread_id) const {
        const auto resolved = ValidateInstance(object, object_class, field);
        const auto hooks = Hooks();
        if (hooks.get_instance) {
            if (auto value = hooks.get_instance(object, object_class, resolved,
                                                thread_id);
                value.has_value()) {
                ValidateValue(resolved.type, *value);
                return *value;
            }
        }
        std::scoped_lock lock(mutex_);
        const auto found = instance_values_.find(InstanceKey(object, field));
        return found == instance_values_.end() ? DefaultValue(resolved.type)
                                               : found->second;
    }

    void SetInstance(const JniObjectIdentity object,
                     const JniObjectIdentity object_class,
                     const JniFieldId field, JniValue value,
                     const std::uint64_t thread_id) {
        const auto resolved = ValidateInstance(object, object_class, field);
        ValidateValue(resolved.type, value);
        const auto hooks = Hooks();
        if (hooks.set_instance && hooks.set_instance(
                object, object_class, resolved, value, thread_id)) {
            return;
        }
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
        const JniFieldId field, const std::uint64_t thread_id) const {
        const auto resolved = ValidateStatic(dispatch_class, field);
        const auto hooks = Hooks();
        if (hooks.get_static) {
            if (auto value = hooks.get_static(dispatch_class, resolved,
                                              thread_id);
                value.has_value()) {
                ValidateValue(resolved.type, *value);
                return *value;
            }
        }
        std::scoped_lock lock(mutex_);
        const auto found = static_values_.find(field.Value());
        return found == static_values_.end() ? DefaultValue(resolved.type)
                                             : found->second;
    }

    void SetStatic(const JniObjectIdentity dispatch_class,
                   const JniFieldId field, JniValue value,
                   const std::uint64_t thread_id) {
        const auto resolved = ValidateStatic(dispatch_class, field);
        ValidateValue(resolved.type, value);
        const auto hooks = Hooks();
        if (hooks.set_static && hooks.set_static(
                dispatch_class, resolved, value, thread_id)) {
            return;
        }
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

    [[nodiscard]] JniFieldAccessHooks Hooks() const {
        std::scoped_lock lock(hooks_mutex_);
        return hooks_;
    }

    const JniClassRegistry* classes_{};
    mutable std::mutex mutex_;
    mutable std::mutex hooks_mutex_;
    JniFieldAccessHooks hooks_;
    std::map<Key, JniValue> instance_values_;
    std::map<std::uint32_t, JniValue> static_values_;
};

JniFieldStore::JniFieldStore(const JniClassRegistry& classes)
    : impl_(std::make_unique<Impl>(classes)) {}
JniFieldStore::~JniFieldStore() = default;
JniFieldStore::JniFieldStore(JniFieldStore&&) noexcept = default;
JniFieldStore& JniFieldStore::operator=(JniFieldStore&&) noexcept = default;

void JniFieldStore::SetAccessHooks(JniFieldAccessHooks hooks) {
    impl_->SetAccessHooks(std::move(hooks));
}
bool JniFieldStore::EnsureClassInitialized(
    const JniObjectIdentity java_class, const std::uint64_t thread_id) const {
    return impl_->EnsureClassInitialized(java_class, thread_id);
}

JniValue JniFieldStore::GetInstance(const JniObjectIdentity object,
                                    const JniObjectIdentity object_class,
                                    const JniFieldId field,
                                    const std::uint64_t thread_id) const {
    return impl_->GetInstance(object, object_class, field, thread_id);
}
void JniFieldStore::SetInstance(const JniObjectIdentity object,
                                const JniObjectIdentity object_class,
                                const JniFieldId field, JniValue value,
                                const std::uint64_t thread_id) {
    impl_->SetInstance(object, object_class, field, std::move(value), thread_id);
}
void JniFieldStore::DeleteInstanceFields(const JniObjectIdentity object) {
    impl_->DeleteInstanceFields(object);
}
JniValue JniFieldStore::GetStatic(const JniObjectIdentity dispatch_class,
                                  const JniFieldId field,
                                  const std::uint64_t thread_id) const {
    return impl_->GetStatic(dispatch_class, field, thread_id);
}
void JniFieldStore::SetStatic(const JniObjectIdentity dispatch_class,
                              const JniFieldId field, JniValue value,
                              const std::uint64_t thread_id) {
    impl_->SetStatic(dispatch_class, field, std::move(value), thread_id);
}

}  // namespace ogplay::runtime
