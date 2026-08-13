#include "ogplay/runtime/dexvm/intrinsic_builder.h"

#include <string_view>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::runtime::dexvm {
namespace {

[[noreturn]] void FailBuild(std::string message) {
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     std::move(message));
}

void ValidateClassDescriptor(const std::string_view descriptor) {
    try {
        const auto parsed = ParseJniFieldDescriptor(descriptor);
        if (parsed.kind != JniTypeKind::object) {
            FailBuild("intrinsic class descriptor is not an object: " +
                      std::string(descriptor));
        }
    } catch (const JniSignatureError& error) {
        FailBuild("invalid intrinsic class descriptor " +
                  std::string(descriptor) + ": " + error.what());
    }
}

void ValidateMethodDescriptor(const std::string_view descriptor) {
    try {
        static_cast<void>(ParseJniMethodDescriptor(descriptor));
    } catch (const JniSignatureError& error) {
        FailBuild("invalid intrinsic method descriptor " +
                  std::string(descriptor) + ": " + error.what());
    }
}

void ValidateFieldDescriptor(const std::string_view descriptor) {
    try {
        static_cast<void>(ParseJniFieldDescriptor(descriptor));
    } catch (const JniSignatureError& error) {
        FailBuild("invalid intrinsic field descriptor " +
                  std::string(descriptor) + ": " + error.what());
    }
}

[[nodiscard]] std::string MemberKey(const std::string& name,
                                    const std::string& descriptor) {
    return name + '\0' + descriptor;
}

}  // namespace

IntrinsicClassBuilder::IntrinsicClassBuilder(std::string descriptor) {
    declaration_.descriptor = std::move(descriptor);
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Super(std::string descriptor) {
    declaration_.superclass = std::move(descriptor);
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Implements(
    std::string descriptor) {
    declaration_.interfaces.push_back(std::move(descriptor));
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::MarkInterface() {
    declaration_.is_interface = true;
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Method(
    std::string name, std::string descriptor, const bool is_static,
    const bool overridable, IntrinsicHandler handler) {
    IntrinsicMethodDecl method;
    method.name = std::move(name);
    method.descriptor = std::move(descriptor);
    method.is_static = is_static;
    method.overridable = overridable;
    method.implementation = std::move(handler);
    declaration_.methods.push_back(std::move(method));
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Static(
    std::string name, std::string descriptor, IntrinsicHandler handler) {
    return Method(std::move(name), std::move(descriptor), true, false,
                  std::move(handler));
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Virtual(
    std::string name, std::string descriptor, IntrinsicHandler handler) {
    return Method(std::move(name), std::move(descriptor), false, false,
                  std::move(handler));
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Overridable(
    std::string name, std::string descriptor, IntrinsicHandler handler) {
    return Method(std::move(name), std::move(descriptor), false, true,
                  std::move(handler));
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Field(
    std::string name, std::string descriptor, const bool is_static) {
    IntrinsicFieldDecl field;
    field.name = std::move(name);
    field.descriptor = std::move(descriptor);
    field.is_static = is_static;
    declaration_.fields.push_back(std::move(field));
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::ConstantInt(
    std::string name, std::string descriptor, const std::int64_t value) {
    IntrinsicFieldDecl field;
    field.name = std::move(name);
    field.descriptor = std::move(descriptor);
    field.is_static = true;
    field.has_constant = true;
    field.integral = value;
    declaration_.fields.push_back(std::move(field));
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::ConstantString(
    std::string name, std::string value) {
    IntrinsicFieldDecl field;
    field.name = std::move(name);
    field.descriptor = "Ljava/lang/String;";
    field.is_static = true;
    field.has_constant = true;
    field.string_value = std::move(value);
    declaration_.fields.push_back(std::move(field));
    return *this;
}

IntrinsicClassBuilder& IntrinsicClassBuilder::Clinit(
    IntrinsicHandler handler) {
    declaration_.clinit_implementation = std::move(handler);
    return *this;
}

IntrinsicClassDecl IntrinsicClassBuilder::Build() && {
    ValidateClassDescriptor(declaration_.descriptor);
    if (declaration_.superclass.has_value()) {
        ValidateClassDescriptor(*declaration_.superclass);
    }
    for (const auto& interface_descriptor : declaration_.interfaces) {
        ValidateClassDescriptor(interface_descriptor);
    }

    std::unordered_set<std::string> method_keys;
    for (const auto& method : declaration_.methods) {
        ValidateMethodDescriptor(method.descriptor);
        if (!method_keys.insert(MemberKey(method.name, method.descriptor))
                 .second) {
            FailBuild("duplicate intrinsic method: " + method.name +
                      method.descriptor);
        }
    }

    std::unordered_set<std::string> field_keys;
    for (const auto& field : declaration_.fields) {
        ValidateFieldDescriptor(field.descriptor);
        if (!field_keys.insert(MemberKey(field.name, field.descriptor)).second) {
            FailBuild("duplicate intrinsic field: " + field.name + ":" +
                      field.descriptor);
        }
        if (declaration_.is_interface && !field.is_static) {
            FailBuild("intrinsic interface has an instance field: " +
                      field.name);
        }
    }
    return std::move(declaration_);
}

}  // namespace ogplay::runtime::dexvm
