#include "ogplay/runtime/jni/jni_object_array.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniObjectArrayErrorReason reason,
                       const char* message) {
    throw JniObjectArrayError(reason, message);
}

}  // namespace

JniObjectArrayError::JniObjectArrayError(
    const JniObjectArrayErrorReason reason, const char* message)
    : std::runtime_error(message), reason_(reason) {}

JniObjectArrayErrorReason JniObjectArrayError::Reason() const noexcept {
    return reason_;
}

class JniObjectArrayStore::Impl final {
public:
    explicit Impl(const JniClassRegistry& classes) : classes_(&classes) {}

    [[nodiscard]] JniObjectIdentity New(
        const JniObjectIdentity element_class, const JniSize length,
        const std::optional<JniObjectValue>& initial) {
        if (length < 0) {
            Fail(JniObjectArrayErrorReason::invalid_length,
                 "JNI object array length is negative");
        }
        ValidateClass(element_class,
                      JniObjectArrayErrorReason::invalid_element_class);
        ValidateValue(element_class, initial);
        const auto identity = AllocateJniHostObjectIdentity();
        std::scoped_lock lock(mutex_);
        arrays_.emplace(identity.value,
                        Entry{element_class,
                              std::vector<std::optional<JniObjectValue>>(
                                  static_cast<std::size_t>(length), initial)});
        return identity;
    }

    void Delete(const JniObjectIdentity array) {
        std::scoped_lock lock(mutex_);
        if (array.domain != JniObjectDomain::host ||
            arrays_.erase(array.value) == 0) {
            UnknownArray();
        }
    }

    [[nodiscard]] bool Contains(const JniObjectIdentity array) const noexcept {
        if (array.domain != JniObjectDomain::host) return false;
        std::scoped_lock lock(mutex_);
        return arrays_.contains(array.value);
    }

    [[nodiscard]] JniSize Length(const JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return static_cast<JniSize>(Require(array).elements.size());
    }

    [[nodiscard]] JniObjectIdentity ElementClass(
        const JniObjectIdentity array) const {
        std::scoped_lock lock(mutex_);
        return Require(array).element_class;
    }

    [[nodiscard]] std::optional<JniObjectValue> Get(
        const JniObjectIdentity array, const JniSize index) const {
        std::scoped_lock lock(mutex_);
        const auto& entry = Require(array);
        return entry.elements[CheckedIndex(entry, index)];
    }

    void Set(const JniObjectIdentity array, const JniSize index,
             const std::optional<JniObjectValue>& value) {
        std::scoped_lock lock(mutex_);
        auto& entry = Require(array);
        ValidateValue(entry.element_class, value);
        entry.elements[CheckedIndex(entry, index)] = value;
    }

private:
    struct Entry final {
        JniObjectIdentity element_class;
        std::vector<std::optional<JniObjectValue>> elements;
    };

    void ValidateClass(const JniObjectIdentity java_class,
                       const JniObjectArrayErrorReason reason) const {
        if (java_class.domain == JniObjectDomain::dex_vm &&
            java_class.value != 0U) {
            return;
        }
        try {
            static_cast<void>(classes_->GetSuperclass(java_class));
        } catch (const JniClassRegistryError&) {
            Fail(reason, "JNI object array class is not registered");
        }
    }

    void ValidateValue(const JniObjectIdentity element_class,
                       const std::optional<JniObjectValue>& value) const {
        if (!value.has_value()) return;
        if (value->object.value == 0 || value->java_class.value == 0) {
            Fail(JniObjectArrayErrorReason::invalid_value,
                 "JNI object array value has a null identity");
        }
        ValidateClass(value->java_class,
                      JniObjectArrayErrorReason::invalid_value);
        if (element_class.domain == JniObjectDomain::dex_vm ||
            value->java_class.domain == JniObjectDomain::dex_vm) {
            if (element_class != value->java_class) {
                Fail(JniObjectArrayErrorReason::incompatible_element,
                     "JNI object array synthetic class is incompatible");
            }
            return;
        }
        if (!classes_->IsAssignableFrom(element_class, value->java_class)) {
            Fail(JniObjectArrayErrorReason::incompatible_element,
                 "JNI object array value is not assignable to element class");
        }
    }

    [[nodiscard]] const Entry& Require(const JniObjectIdentity array) const {
        if (array.domain != JniObjectDomain::host) UnknownArray();
        const auto found = arrays_.find(array.value);
        if (found == arrays_.end()) UnknownArray();
        return found->second;
    }

    [[nodiscard]] Entry& Require(const JniObjectIdentity array) {
        return const_cast<Entry&>(std::as_const(*this).Require(array));
    }

    [[nodiscard]] static std::size_t CheckedIndex(const Entry& entry,
                                                  const JniSize index) {
        if (index < 0 ||
            static_cast<std::size_t>(index) >= entry.elements.size()) {
            Fail(JniObjectArrayErrorReason::invalid_index,
                 "JNI object array index is out of bounds");
        }
        return static_cast<std::size_t>(index);
    }

    [[noreturn]] static void UnknownArray() {
        Fail(JniObjectArrayErrorReason::unknown_array,
             "JNI object array identity is unknown");
    }

    const JniClassRegistry* classes_{};
    mutable std::mutex mutex_;
    std::map<std::uint64_t, Entry> arrays_;
};

JniObjectArrayStore::JniObjectArrayStore(const JniClassRegistry& classes)
    : impl_(std::make_unique<Impl>(classes)) {}
JniObjectArrayStore::~JniObjectArrayStore() = default;
JniObjectArrayStore::JniObjectArrayStore(JniObjectArrayStore&&) noexcept =
    default;
JniObjectArrayStore& JniObjectArrayStore::operator=(
    JniObjectArrayStore&&) noexcept = default;

JniObjectIdentity JniObjectArrayStore::New(
    const JniObjectIdentity element_class, const JniSize length,
    std::optional<JniObjectValue> initial) {
    return impl_->New(element_class, length, initial);
}
void JniObjectArrayStore::Delete(const JniObjectIdentity array) {
    impl_->Delete(array);
}
bool JniObjectArrayStore::Contains(
    const JniObjectIdentity array) const noexcept {
    return impl_->Contains(array);
}
JniSize JniObjectArrayStore::Length(const JniObjectIdentity array) const {
    return impl_->Length(array);
}
JniObjectIdentity JniObjectArrayStore::ElementClass(
    const JniObjectIdentity array) const {
    return impl_->ElementClass(array);
}
std::optional<JniObjectValue> JniObjectArrayStore::Get(
    const JniObjectIdentity array, const JniSize index) const {
    return impl_->Get(array, index);
}
void JniObjectArrayStore::Set(const JniObjectIdentity array,
                              const JniSize index,
                              std::optional<JniObjectValue> value) {
    impl_->Set(array, index, value);
}

}  // namespace ogplay::runtime
