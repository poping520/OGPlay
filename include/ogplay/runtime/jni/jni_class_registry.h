#pragma once

#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "ogplay/runtime/jni/jni.h"
#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::runtime {

struct JniMethodDeclaration final {
    std::string name;
    std::string descriptor;
    std::string implementation;
    bool is_static{};
};

struct JniFieldDeclaration final {
    std::string name;
    std::string descriptor;
    std::string implementation;
    bool is_static{};
};

struct JniClassDeclaration final {
    std::string name;
    std::optional<std::string> superclass;
    std::vector<JniMethodDeclaration> methods;
    std::vector<JniFieldDeclaration> fields;
};

struct JniResolvedMethod final {
    JniObjectIdentity declaring_class;
    JniMethodId id;
    JniMethodDeclaration declaration;
    JniMethodDescriptor layout;
};

struct JniResolvedField final {
    JniObjectIdentity declaring_class;
    JniFieldId id;
    JniFieldDeclaration declaration;
    JniTypeDescriptor type;
};

enum class JniClassRegistryErrorReason : std::uint8_t {
    invalid_class,
    unknown_superclass,
    duplicate_class,
    invalid_member,
    duplicate_member,
    id_space_exhausted,
};

class JniClassRegistryError final : public std::runtime_error {
public:
    JniClassRegistryError(JniClassRegistryErrorReason reason,
                          std::string message);
    [[nodiscard]] JniClassRegistryErrorReason Reason() const noexcept;

private:
    JniClassRegistryErrorReason reason_;
};

class JniClassRegistry final {
public:
    JniClassRegistry();
    ~JniClassRegistry();
    JniClassRegistry(const JniClassRegistry&) = delete;
    JniClassRegistry& operator=(const JniClassRegistry&) = delete;
    JniClassRegistry(JniClassRegistry&&) noexcept;
    JniClassRegistry& operator=(JniClassRegistry&&) noexcept;

  [[nodiscard]] JniObjectIdentity
  RegisterClass(const JniClassDeclaration &declaration);
  // Adds one declared method to an existing class. This is used when two
  // platform providers share a class identity but own disjoint methods;
  // an existing declaration with the same name/descriptor is rejected.
  [[nodiscard]] JniMethodId
  RegisterMethod(JniObjectIdentity java_class,
                 const JniMethodDeclaration &declaration);
  // Adds one declared field to an existing class. DexVM uses this when a
  // code-defined class shares an already-published platform class identity.
  [[nodiscard]] JniFieldId
  RegisterField(JniObjectIdentity java_class,
                const JniFieldDeclaration& declaration);
  [[nodiscard]] std::optional<JniObjectIdentity>
  FindClass(const std::string &name) const;
  [[nodiscard]] std::optional<JniObjectIdentity>
  GetSuperclass(JniObjectIdentity java_class) const;
    [[nodiscard]] bool IsAssignableFrom(JniObjectIdentity target,
                                        JniObjectIdentity source) const;

  [[nodiscard]] std::optional<JniMethodId>
  GetMethodId(JniObjectIdentity java_class, const std::string &name,
        const std::string& descriptor, bool is_static) const;
  [[nodiscard]] std::optional<JniFieldId>
  GetFieldId(JniObjectIdentity java_class, const std::string &name,
        const std::string& descriptor, bool is_static) const;
    [[nodiscard]] JniResolvedMethod ResolveMethod(JniMethodId id) const;
    [[nodiscard]] JniResolvedField ResolveField(JniFieldId id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
