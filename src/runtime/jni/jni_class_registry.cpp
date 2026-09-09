#include "ogplay/runtime/jni/jni_class_registry.h"

#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/jni/jni_object.h"

namespace ogplay::runtime {
namespace {

[[noreturn]] void Fail(const JniClassRegistryErrorReason reason,
                       std::string message) {
    throw JniClassRegistryError(reason, std::move(message));
}

void ValidateClassName(const std::string& name) {
    try {
        const auto type = ParseJniFieldDescriptor("L" + name + ";");
        if (type.kind != JniTypeKind::object || type.object_class != name) {
            Fail(JniClassRegistryErrorReason::invalid_class,
                 "JNI class name is not an object internal name");
        }
    } catch (const JniSignatureError& error) {
        Fail(JniClassRegistryErrorReason::invalid_class,
             std::string("invalid JNI class name: ") + error.what());
    }
}

void ValidateMemberName(const std::string& name) {
    if (name.empty() || name.find('\0') != std::string::npos ||
        name.find('/') != std::string::npos ||
        name.find('.') != std::string::npos ||
        name.find(';') != std::string::npos ||
        name.find('[') != std::string::npos ||
        name.find('(') != std::string::npos) {
        Fail(JniClassRegistryErrorReason::invalid_member,
             "JNI member has an invalid name");
    }
}

struct MemberKey final {
    std::string name;
    std::string descriptor;
    auto operator<=>(const MemberKey&) const = default;
};

}  // namespace

JniClassRegistryError::JniClassRegistryError(
    const JniClassRegistryErrorReason reason, std::string message)
    : std::runtime_error(std::move(message)), reason_(reason) {}

JniClassRegistryErrorReason JniClassRegistryError::Reason() const noexcept {
    return reason_;
}

class JniClassRegistry::Impl final {
public:
  [[nodiscard]] JniObjectIdentity
  Register(const JniClassDeclaration &declaration) {
        ValidateClassName(declaration.name);
        std::set<MemberKey> method_keys;
        std::vector<JniMethodDescriptor> method_layouts;
        method_layouts.reserve(declaration.methods.size());
        for (const auto& method : declaration.methods) {
            ValidateMemberName(method.name);
      if (method.implementation.empty())
        InvalidMember();
            try {
        method_layouts.push_back(ParseJniMethodDescriptor(method.descriptor));
            } catch (const JniSignatureError& error) {
                InvalidMember(error.what());
            }
            if (!method_keys.emplace(method.name, method.descriptor).second) {
                DuplicateMember();
            }
        }

        std::set<MemberKey> field_keys;
        std::vector<JniTypeDescriptor> field_types;
        field_types.reserve(declaration.fields.size());
        for (const auto& field : declaration.fields) {
            ValidateMemberName(field.name);
      if (field.implementation.empty())
        InvalidMember();
            try {
                field_types.push_back(ParseJniFieldDescriptor(field.descriptor));
            } catch (const JniSignatureError& error) {
                InvalidMember(error.what());
            }
            if (!field_keys.emplace(field.name, field.descriptor).second) {
                DuplicateMember();
            }
        }

        std::scoped_lock lock(mutex_);
        if (classes_by_name_.contains(declaration.name)) {
            Fail(JniClassRegistryErrorReason::duplicate_class,
                 "JNI class is already registered");
        }
        std::optional<JniObjectIdentity> superclass;
        if (declaration.superclass.has_value()) {
            const auto found = classes_by_name_.find(*declaration.superclass);
            if (found == classes_by_name_.end()) {
                Fail(JniClassRegistryErrorReason::unknown_superclass,
                     "JNI superclass must be registered first");
            }
            superclass = found->second;
        }
        std::vector<JniObjectIdentity> interfaces;
        interfaces.reserve(declaration.interfaces.size());
        std::unordered_set<std::uint64_t> unique_interfaces;
        for (const auto& interface_name : declaration.interfaces) {
            const auto found = classes_by_name_.find(interface_name);
            if (found == classes_by_name_.end()) {
                Fail(JniClassRegistryErrorReason::unknown_interface,
                     "JNI interface must be registered first");
            }
            if (!RequireClass(found->second).is_interface) {
                Fail(JniClassRegistryErrorReason::invalid_class,
                     "JNI implemented type is not an interface");
            }
            if (!unique_interfaces.emplace(found->second.value).second) {
                Fail(JniClassRegistryErrorReason::duplicate_interface,
                     "JNI class contains a duplicate interface");
            }
            interfaces.push_back(found->second);
        }
        EnsureIds(declaration.methods.size(), declaration.fields.size());

        ClassEntry entry{declaration.name, superclass, std::move(interfaces),
                         declaration.is_interface, {}, {}};
    for (std::size_t index = 0; index < declaration.methods.size(); ++index) {
            const JniMethodId id{next_method_id_++};
            const auto& method = declaration.methods[index];
            entry.methods.emplace(MemberKey{method.name, method.descriptor}, id);
      methods_.emplace(
          id.Value(), JniResolvedMethod{{}, id, method, method_layouts[index]});
        }
        for (std::size_t index = 0; index < declaration.fields.size(); ++index) {
            const JniFieldId id{next_field_id_++};
            const auto& field = declaration.fields[index];
            entry.fields.emplace(MemberKey{field.name, field.descriptor}, id);
            fields_.emplace(id.Value(),
                            JniResolvedField{{}, id, field, field_types[index]});
        }

        const auto identity = AllocateJniHostObjectIdentity();
        for (auto& [key, id] : entry.methods) {
            static_cast<void>(key);
            methods_.at(id.Value()).declaring_class = identity;
        }
        for (auto& [key, id] : entry.fields) {
            static_cast<void>(key);
            fields_.at(id.Value()).declaring_class = identity;
        }
        classes_by_name_.emplace(declaration.name, identity);
        classes_.emplace(identity.value, std::move(entry));
        return identity;
    }

  [[nodiscard]] JniMethodId
  RegisterMethod(const JniObjectIdentity java_class,
                 const JniMethodDeclaration &declaration) {
    ValidateMemberName(declaration.name);
    if (declaration.implementation.empty())
      InvalidMember();
    JniMethodDescriptor layout;
    try {
      layout = ParseJniMethodDescriptor(declaration.descriptor);
    } catch (const JniSignatureError &error) {
      InvalidMember(error.what());
    }
    std::scoped_lock lock(mutex_);
    if (java_class.domain != JniObjectDomain::host)
      UnknownClass();
    const auto found = classes_.find(java_class.value);
    if (found == classes_.end())
      UnknownClass();
    auto &entry = found->second;
    const MemberKey key{declaration.name, declaration.descriptor};
    if (entry.methods.contains(key))
      DuplicateMember();
    EnsureIds(1U, 0U);
    const JniMethodId id{next_method_id_++};
    entry.methods.emplace(key, id);
    methods_.emplace(id.Value(), JniResolvedMethod{java_class, id, declaration,
                                                   std::move(layout)});
    return id;
  }

  [[nodiscard]] JniFieldId
  RegisterField(const JniObjectIdentity java_class,
                const JniFieldDeclaration& declaration) {
    ValidateMemberName(declaration.name);
    if (declaration.implementation.empty()) InvalidMember();
    JniTypeDescriptor type;
    try {
      type = ParseJniFieldDescriptor(declaration.descriptor);
    } catch (const JniSignatureError& error) {
      InvalidMember(error.what());
    }
    std::scoped_lock lock(mutex_);
    if (java_class.domain != JniObjectDomain::host) UnknownClass();
    const auto found = classes_.find(java_class.value);
    if (found == classes_.end()) UnknownClass();
    auto& entry = found->second;
    const MemberKey key{declaration.name, declaration.descriptor};
    if (entry.fields.contains(key)) DuplicateMember();
    EnsureIds(0U, 1U);
    const JniFieldId id{next_field_id_++};
    entry.fields.emplace(key, id);
    fields_.emplace(id.Value(), JniResolvedField{
        java_class, id, declaration, std::move(type)});
    return id;
  }

  [[nodiscard]] std::optional<JniObjectIdentity>
  Find(const std::string &name) const {
        std::scoped_lock lock(mutex_);
        const auto found = classes_by_name_.find(name);
        return found == classes_by_name_.end()
                   ? std::nullopt
                   : std::optional<JniObjectIdentity>{found->second};
    }

  [[nodiscard]] std::string ClassName(
      const JniObjectIdentity java_class) const {
        std::scoped_lock lock(mutex_);
        return RequireClass(java_class).name;
    }

  [[nodiscard]] std::optional<JniObjectIdentity>
  Superclass(const JniObjectIdentity java_class) const {
        std::scoped_lock lock(mutex_);
        return RequireClass(java_class).superclass;
    }

  [[nodiscard]] std::vector<JniObjectIdentity>
  Interfaces(const JniObjectIdentity java_class) const {
        std::scoped_lock lock(mutex_);
        return RequireClass(java_class).interfaces;
    }

    [[nodiscard]] bool Assignable(const JniObjectIdentity target,
                                  JniObjectIdentity source) const {
        std::scoped_lock lock(mutex_);
        static_cast<void>(RequireClass(target));
        std::vector<JniObjectIdentity> pending{source};
        std::unordered_set<std::uint64_t> visited;
        while (!pending.empty()) {
            source = pending.back();
            pending.pop_back();
            if (source == target) return true;
            if (!visited.emplace(source.value).second) continue;
            const auto& source_entry = RequireClass(source);
            if (source_entry.superclass.has_value()) {
                pending.push_back(*source_entry.superclass);
            }
            pending.insert(pending.end(), source_entry.interfaces.begin(),
                           source_entry.interfaces.end());
        }
        return false;
    }

  [[nodiscard]] std::optional<JniMethodId> Method(JniObjectIdentity java_class,
                                                  const std::string &name,
                                                  const std::string &descriptor,
                                                  const bool is_static) const {
        ValidateMemberName(name);
        static_cast<void>(ParseJniMethodDescriptor(descriptor));
        std::scoped_lock lock(mutex_);
        const bool constructor = name == "<init>";
        if (!constructor && RequireClass(java_class).is_interface) {
            std::vector<JniObjectIdentity> pending{java_class};
            std::unordered_set<std::uint64_t> visited;
            while (!pending.empty()) {
                const auto current = pending.back();
                pending.pop_back();
                if (!visited.emplace(current.value).second) continue;
                const auto& entry = RequireClass(current);
                const auto found = entry.methods.find({name, descriptor});
                if (found != entry.methods.end()) {
                    const auto& resolved = methods_.at(found->second.Value());
                    return resolved.declaration.is_static == is_static
                               ? std::optional<JniMethodId>{found->second}
                               : std::nullopt;
                }
                pending.insert(pending.end(), entry.interfaces.rbegin(),
                               entry.interfaces.rend());
            }
            return std::nullopt;
        }
        while (true) {
            const auto& entry = RequireClass(java_class);
            const auto found = entry.methods.find({name, descriptor});
            if (found != entry.methods.end()) {
                const auto& resolved = methods_.at(found->second.Value());
                return resolved.declaration.is_static == is_static
                           ? std::optional<JniMethodId>{found->second}
                           : std::nullopt;
            }
      if (constructor || !entry.superclass.has_value())
        return std::nullopt;
            java_class = *entry.superclass;
        }
    }

  [[nodiscard]] std::optional<JniFieldId> Field(JniObjectIdentity java_class,
                                                const std::string &name,
                                                const std::string &descriptor,
                                                const bool is_static) const {
        ValidateMemberName(name);
        static_cast<void>(ParseJniFieldDescriptor(descriptor));
        std::scoped_lock lock(mutex_);
        const MemberKey key{name, descriptor};
        const auto declared_field = [&](const ClassEntry& entry) {
            const auto found = entry.fields.find(key);
            if (found == entry.fields.end()) {
                return std::optional<JniFieldId>{};
            }
            const auto& resolved = fields_.at(found->second.Value());
            return resolved.declaration.is_static == is_static
                       ? std::optional<JniFieldId>{found->second}
                       : std::optional<JniFieldId>{};
        };
        const auto interface_field =
            [&](auto&& self, const JniObjectIdentity interface_class,
                std::unordered_set<std::uint64_t>& visited)
            -> std::optional<JniFieldId> {
            if (!visited.emplace(interface_class.value).second) {
                return std::nullopt;
            }
            const auto& entry = RequireClass(interface_class);
            if (const auto found = declared_field(entry); found.has_value()) {
                return found;
            }
            for (const auto parent : entry.interfaces) {
                if (const auto found = self(self, parent, visited);
                    found.has_value()) {
                    return found;
                }
            }
            return std::nullopt;
        };
        while (true) {
            const auto& entry = RequireClass(java_class);
            if (const auto found = declared_field(entry); found.has_value()) {
                return found;
            }
            if (is_static) {
                std::unordered_set<std::uint64_t> visited;
                for (const auto interface_class : entry.interfaces) {
                    if (const auto found = interface_field(
                            interface_field, interface_class, visited);
                        found.has_value()) {
                        return found;
                    }
                }
            }
      if (!entry.superclass.has_value())
        return std::nullopt;
            java_class = *entry.superclass;
        }
    }

    [[nodiscard]] JniResolvedMethod ResolveMethod(const JniMethodId id) const {
        std::scoped_lock lock(mutex_);
        const auto found = methods_.find(id.Value());
    if (found == methods_.end())
      InvalidMember();
        return found->second;
    }

    [[nodiscard]] JniResolvedField ResolveField(const JniFieldId id) const {
        std::scoped_lock lock(mutex_);
        const auto found = fields_.find(id.Value());
    if (found == fields_.end())
      InvalidMember();
        return found->second;
    }

private:
    struct ClassEntry final {
        std::string name;
        std::optional<JniObjectIdentity> superclass;
        std::vector<JniObjectIdentity> interfaces;
        bool is_interface{};
        std::map<MemberKey, JniMethodId> methods;
        std::map<MemberKey, JniFieldId> fields;
    };

  [[nodiscard]] const ClassEntry &
  RequireClass(const JniObjectIdentity java_class) const {
    if (java_class.domain != JniObjectDomain::host)
      UnknownClass();
        const auto found = classes_.find(java_class.value);
    if (found == classes_.end())
      UnknownClass();
        return found->second;
    }

    void EnsureIds(const std::size_t methods, const std::size_t fields) const {
        const auto maximum = std::numeric_limits<std::uint32_t>::max();
        if (methods > maximum - next_method_id_ + 1ULL ||
            fields > maximum - next_field_id_ + 1ULL) {
            Fail(JniClassRegistryErrorReason::id_space_exhausted,
                 "JNI member ID space is exhausted");
        }
    }

    [[noreturn]] static void UnknownClass() {
        Fail(JniClassRegistryErrorReason::invalid_class,
             "JNI class identity is not registered");
    }

    [[noreturn]] static void InvalidMember(const char* detail = nullptr) {
        Fail(JniClassRegistryErrorReason::invalid_member,
             detail == nullptr ? "JNI member ID or declaration is invalid"
                               : std::string("invalid JNI member: ") + detail);
    }

    [[noreturn]] static void DuplicateMember() {
        Fail(JniClassRegistryErrorReason::duplicate_member,
             "JNI class contains a duplicate member signature");
    }

    mutable std::mutex mutex_;
    std::map<std::string, JniObjectIdentity> classes_by_name_;
    std::map<std::uint64_t, ClassEntry> classes_;
    std::map<std::uint32_t, JniResolvedMethod> methods_;
    std::map<std::uint32_t, JniResolvedField> fields_;
    std::uint32_t next_method_id_{1};
    std::uint32_t next_field_id_{1};
};

JniClassRegistry::JniClassRegistry() : impl_(std::make_unique<Impl>()) {}
JniClassRegistry::~JniClassRegistry() = default;
JniClassRegistry::JniClassRegistry(JniClassRegistry&&) noexcept = default;
JniClassRegistry &
JniClassRegistry::operator=(JniClassRegistry &&) noexcept = default;

JniObjectIdentity
JniClassRegistry::RegisterClass(const JniClassDeclaration &declaration) {
    return impl_->Register(declaration);
}
JniMethodId
JniClassRegistry::RegisterMethod(const JniObjectIdentity java_class,
                                 const JniMethodDeclaration &declaration) {
  return impl_->RegisterMethod(java_class, declaration);
}
JniFieldId
JniClassRegistry::RegisterField(const JniObjectIdentity java_class,
                                const JniFieldDeclaration& declaration) {
  return impl_->RegisterField(java_class, declaration);
}
std::optional<JniObjectIdentity>
JniClassRegistry::FindClass(const std::string &name) const {
    return impl_->Find(name);
}
std::string JniClassRegistry::ClassName(
    const JniObjectIdentity java_class) const {
  return impl_->ClassName(java_class);
}
std::optional<JniObjectIdentity>
JniClassRegistry::GetSuperclass(const JniObjectIdentity java_class) const {
    return impl_->Superclass(java_class);
}

std::vector<JniObjectIdentity> JniClassRegistry::GetInterfaces(
    const JniObjectIdentity java_class) const {
    return impl_->Interfaces(java_class);
}
bool JniClassRegistry::IsAssignableFrom(const JniObjectIdentity target,
                                        const JniObjectIdentity source) const {
    return impl_->Assignable(target, source);
}
std::optional<JniMethodId> JniClassRegistry::GetMethodId(
    const JniObjectIdentity java_class, const std::string& name,
    const std::string& descriptor, const bool is_static) const {
    return impl_->Method(java_class, name, descriptor, is_static);
}
std::optional<JniFieldId> JniClassRegistry::GetFieldId(
    const JniObjectIdentity java_class, const std::string& name,
    const std::string& descriptor, const bool is_static) const {
    return impl_->Field(java_class, name, descriptor, is_static);
}
JniResolvedMethod JniClassRegistry::ResolveMethod(const JniMethodId id) const {
    return impl_->ResolveMethod(id);
}
JniResolvedField JniClassRegistry::ResolveField(const JniFieldId id) const {
    return impl_->ResolveField(id);
}

}  // namespace ogplay::runtime
