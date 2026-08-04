#include "ogplay/runtime/jni_native_registry.h"

#include <compare>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <utility>

#include "ogplay/runtime/jni_signature.h"

namespace ogplay::runtime {
namespace {

struct ClassKey final {
    JniObjectDomain domain{JniObjectDomain::host};
    std::uint64_t value{};
    auto operator<=>(const ClassKey&) const = default;
};

struct MethodKey final {
    std::string name;
    std::string descriptor;
    auto operator<=>(const MethodKey&) const = default;
};

[[noreturn]] void Fail(const JniNativeRegistryErrorReason reason,
                       std::string message) {
    throw JniNativeRegistryError(reason, std::move(message));
}

void ValidateClass(const JniObjectIdentity java_class) {
    if (java_class.value == 0) {
        Fail(JniNativeRegistryErrorReason::invalid_class,
             "RegisterNatives class identity cannot be null");
    }
}

void ValidateMethod(const JniNativeMethod& method) {
    if (method.name.empty() || method.name.find('\0') != std::string::npos) {
        Fail(JniNativeRegistryErrorReason::invalid_name,
             "RegisterNatives method name is empty or contains NUL");
    }
    try {
        static_cast<void>(ParseJniMethodDescriptor(method.descriptor));
    } catch (const JniSignatureError& error) {
        Fail(JniNativeRegistryErrorReason::invalid_descriptor,
             std::string("invalid RegisterNatives descriptor: ") + error.what());
    }
    if (method.target.IsNull()) {
        Fail(JniNativeRegistryErrorReason::invalid_target,
             "RegisterNatives target cannot be null");
    }
}

[[nodiscard]] ClassKey ToKey(const JniObjectIdentity identity) {
    return {identity.domain, identity.value};
}

}  // namespace

JniNativeRegistryError::JniNativeRegistryError(
    const JniNativeRegistryErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

JniNativeRegistryErrorReason JniNativeRegistryError::Reason() const noexcept {
    return reason_;
}

class JniNativeRegistry::Impl final {
public:
    void Register(const JniObjectIdentity java_class,
                  const std::span<const JniNativeMethod> methods) {
        ValidateClass(java_class);
        std::set<MethodKey> batch;
        for (const auto& method : methods) {
            ValidateMethod(method);
            if (!batch.emplace(method.name, method.descriptor).second) {
                Fail(JniNativeRegistryErrorReason::duplicate_declaration,
                     "RegisterNatives batch contains a duplicate method");
            }
        }

        std::scoped_lock lock(mutex_);
        const auto class_key = ToKey(java_class);
        const auto existing_class = classes_.find(class_key);
        if (existing_class != classes_.end()) {
            for (const auto& method : methods) {
                const auto existing = existing_class->second.find(
                    MethodKey{method.name, method.descriptor});
                if (existing != existing_class->second.end() &&
                    existing->second != method.target) {
                    Fail(JniNativeRegistryErrorReason::conflicting_declaration,
                         "RegisterNatives method already has another target");
                }
            }
        }

        auto& destination = classes_[class_key];
        for (const auto& method : methods) {
            destination.emplace(MethodKey{method.name, method.descriptor},
                                method.target);
        }
    }

    [[nodiscard]] std::size_t Unregister(
        const JniObjectIdentity java_class) {
        ValidateClass(java_class);
        std::scoped_lock lock(mutex_);
        const auto found = classes_.find(ToKey(java_class));
        if (found == classes_.end()) return 0;
        const auto count = found->second.size();
        classes_.erase(found);
        return count;
    }

    [[nodiscard]] std::optional<memory::GuestAddress> Resolve(
        const JniObjectIdentity java_class, const std::string& name,
        const std::string& descriptor) const {
        ValidateClass(java_class);
        ValidateMethod({name, descriptor, memory::GuestAddress{1}});
        std::scoped_lock lock(mutex_);
        const auto found_class = classes_.find(ToKey(java_class));
        if (found_class == classes_.end()) return std::nullopt;
        const auto found_method =
            found_class->second.find(MethodKey{name, descriptor});
        if (found_method == found_class->second.end()) return std::nullopt;
        return found_method->second;
    }

    [[nodiscard]] std::vector<JniNativeMethod> Declarations(
        const JniObjectIdentity java_class) const {
        ValidateClass(java_class);
        std::scoped_lock lock(mutex_);
        std::vector<JniNativeMethod> result;
        const auto found = classes_.find(ToKey(java_class));
        if (found == classes_.end()) return result;
        result.reserve(found->second.size());
        for (const auto& [key, target] : found->second) {
            result.push_back({key.name, key.descriptor, target});
        }
        return result;
    }

private:
    using MethodMap = std::map<MethodKey, memory::GuestAddress>;
    mutable std::mutex mutex_;
    std::map<ClassKey, MethodMap> classes_;
};

JniNativeRegistry::JniNativeRegistry() : impl_(std::make_unique<Impl>()) {}
JniNativeRegistry::~JniNativeRegistry() = default;
JniNativeRegistry::JniNativeRegistry(JniNativeRegistry&&) noexcept = default;
JniNativeRegistry& JniNativeRegistry::operator=(
    JniNativeRegistry&&) noexcept = default;

void JniNativeRegistry::RegisterNatives(
    const JniObjectIdentity java_class,
    const std::span<const JniNativeMethod> methods) {
    impl_->Register(java_class, methods);
}

std::size_t JniNativeRegistry::UnregisterNatives(
    const JniObjectIdentity java_class) {
    return impl_->Unregister(java_class);
}

std::optional<memory::GuestAddress> JniNativeRegistry::Resolve(
    const JniObjectIdentity java_class, const std::string& name,
    const std::string& descriptor) const {
    return impl_->Resolve(java_class, name, descriptor);
}

std::vector<JniNativeMethod> JniNativeRegistry::Declarations(
    const JniObjectIdentity java_class) const {
    return impl_->Declarations(java_class);
}

}  // namespace ogplay::runtime
