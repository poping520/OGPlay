#include "ogplay/runtime/dexvm/intrinsic_builder.h"

#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "ogplay/runtime/jni/jni_signature.h"

namespace ogplay::runtime::dexvm {
    namespace {
        [[noreturn]] void FailBuild(std::string message) {
            throw DexVmError(DexVmErrorReason::internal_invariant, std::move(message));
        }

        void ValidateConstructorDescriptor(const std::string_view descriptor) {
            try {
                const auto parsed = ParseJniMethodDescriptor(descriptor);
                if (parsed.result.kind != JniTypeKind::void_value) {
                    FailBuild("intrinsic constructor must return void: " + std::string(descriptor));
                }
            } catch (const JniSignatureError& error) {
                FailBuild(
                    "invalid intrinsic constructor descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateClassDescriptor(const std::string_view descriptor) {
            try {
                const auto parsed = ParseJniFieldDescriptor(descriptor);
                if (parsed.kind != JniTypeKind::object) {
                    FailBuild("intrinsic class descriptor is not an object: " + std::string(descriptor));
                }
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic class descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateMethodDescriptor(const std::string_view descriptor) {
            try {
                static_cast<void>(ParseJniMethodDescriptor(descriptor));
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic method descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        void ValidateFieldDescriptor(const std::string_view descriptor) {
            try {
                static_cast<void>(ParseJniFieldDescriptor(descriptor));
            } catch (const JniSignatureError& error) {
                FailBuild("invalid intrinsic field descriptor " + std::string(descriptor) + ": " + error.what());
            }
        }

        [[nodiscard]] std::string MemberKey(const std::string& name,
                                            const std::string& descriptor) {
            return name + '\0' + descriptor;
        }

        void ValidateImplementedHandler(const IntrinsicHandler& handler,
                                        const std::string_view member) {
            if (!handler) {
                FailBuild("intrinsic implemented member has an empty handler: " + std::string(member));
            }
        }

        void ValidateOrdinaryMethodName(const std::string_view name) {
            if (name == "<init>" || name == "<clinit>") {
                FailBuild("intrinsic ordinary method uses reserved name: " + std::string(name));
            }
        }

        void ValidateIntegralConstant(const IntrinsicFieldDecl& field) {
            const auto value = field.integral;
            const auto in_range = [value](const std::int64_t minimum, const std::int64_t maximum) {
                return value >= minimum && value <= maximum;
            };

            bool valid = false;
            if (field.descriptor == "Z") {
                valid = value == 0 || value == 1;
            } else if (field.descriptor == "B") {
                valid = in_range(std::numeric_limits<std::int8_t>::min(), std::numeric_limits<std::int8_t>::max());
            } else if (field.descriptor == "C") {
                valid = in_range(0, std::numeric_limits<std::uint16_t>::max());
            } else if (field.descriptor == "S") {
                valid = in_range(std::numeric_limits<std::int16_t>::min(), std::numeric_limits<std::int16_t>::max());
            } else if (field.descriptor == "I") {
                valid = in_range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
            } else if (field.descriptor == "J" || field.descriptor == "D") {
                valid = true;
            } else if (field.descriptor == "F") {
                // Float and double constants retain the existing raw IEEE-754 bit
                // representation used by IntrinsicFieldDecl.
                valid = in_range(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
            }

            if (!valid) {
                FailBuild("intrinsic integral constant has incompatible descriptor " +
                          field.descriptor + " or out-of-range value: " + field.name);
            }
        }
    } // namespace

    IntrinsicClassBuilder IntrinsicClassBuilder::RootClass(std::string descriptor) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.superclass = std::nullopt;
        return builder;
    }

    IntrinsicClassBuilder IntrinsicClassBuilder::Class(
        std::string descriptor, std::optional<std::string> superclass,
        std::vector<std::string> interfaces) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.superclass = std::move(superclass);
        builder.declaration_.interfaces = std::move(interfaces);
        return builder;
    }

    IntrinsicClassBuilder IntrinsicClassBuilder::Interface(
        std::string descriptor, std::vector<std::string> super_interfaces) {
        IntrinsicClassBuilder builder(std::move(descriptor));
        builder.declaration_.interfaces = std::move(super_interfaces);
        builder.declaration_.is_interface = true;
        return builder;
    }

    IntrinsicClassBuilder::IntrinsicClassBuilder(std::string descriptor) {
        declaration_.descriptor = std::move(descriptor);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Method(
        std::string name, std::string descriptor, const MethodType type,
        IntrinsicHandler handler) {
        IntrinsicMethodDecl method;
        method.name = std::move(name);
        method.descriptor = std::move(descriptor);
        method.is_static = type == MethodType::static_method;
        method.overridable = type == MethodType::virtual_method;
        method.implementation = std::move(handler);
        declaration_.methods.push_back(std::move(method));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Constructor(
        std::string descriptor, IntrinsicHandler handler) {
        ValidateConstructorDescriptor(descriptor);
        ValidateImplementedHandler(handler, "<init>");
        return Method("<init>", std::move(descriptor), MethodType::constructor, std::move(handler));
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::StaticMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor), MethodType::static_method, std::move(handler));
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::VirtualMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor), MethodType::virtual_method, std::move(handler));
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::FinalMethod(
        std::string name, std::string descriptor, IntrinsicHandler handler) {
        ValidateOrdinaryMethodName(name);
        ValidateImplementedHandler(handler, name);
        return Method(std::move(name), std::move(descriptor), MethodType::final_method, std::move(handler));
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedMethod(
        std::string name, std::string descriptor, const MethodType type) {
        return Method(std::move(name), std::move(descriptor), type, {});
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedStatic(
        std::string name, std::string descriptor) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor), MethodType::static_method);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedConstructor(
        std::string descriptor) {
        ValidateConstructorDescriptor(descriptor);
        return UnimplementedMethod("<init>", std::move(descriptor), MethodType::constructor);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedVirtual(
        std::string name, std::string descriptor) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor), MethodType::virtual_method);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::UnimplementedFinal(
        std::string name, std::string descriptor) {
        ValidateOrdinaryMethodName(name);
        return UnimplementedMethod(std::move(name), std::move(descriptor), MethodType::final_method);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::Field(
        std::string name, std::string descriptor, const FieldType type) {
        IntrinsicFieldDecl field;
        field.name = std::move(name);
        field.descriptor = std::move(descriptor);
        field.is_static = type == FieldType::static_field;
        declaration_.fields.push_back(std::move(field));
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::InstanceField(
        std::string name, std::string descriptor) {
        return Field(std::move(name), std::move(descriptor), FieldType::instance);
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::StaticField(
        std::string name, std::string descriptor) {
        return Field(std::move(name), std::move(descriptor), FieldType::static_field);
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

    IntrinsicClassBuilder& IntrinsicClassBuilder::ClassInitializer(
        IntrinsicHandler handler) {
        ValidateImplementedHandler(handler, "<clinit>");
        declaration_.clinit_implementation = std::move(handler);
        return *this;
    }

    IntrinsicClassBuilder& IntrinsicClassBuilder::HostStateDestructor(
        ogplay::runtime::dexvm::HostStateDestructor destructor) {
        if (!destructor) {
            FailBuild("intrinsic host-state destructor is empty");
        }
        declaration_.host_state_destructor = std::move(destructor);
        return *this;
    }

    IntrinsicClassDecl IntrinsicClassBuilder::Build() && {
        ValidateClassDescriptor(declaration_.descriptor);

        if (declaration_.superclass.has_value()) {
            ValidateClassDescriptor(*declaration_.superclass);

            if (*declaration_.superclass == declaration_.descriptor) {
                FailBuild("intrinsic class cannot extend itself: " + declaration_.descriptor);
            }
        }

        for (const auto& interface_descriptor: declaration_.interfaces) {
            ValidateClassDescriptor(interface_descriptor);
        }

        std::unordered_set<std::string> method_keys;
        for (const auto& method: declaration_.methods) {
            ValidateMethodDescriptor(method.descriptor);

            if (!method_keys.insert(MemberKey(method.name, method.descriptor)).second) {
                FailBuild("duplicate intrinsic method: " + method.name + method.descriptor);
            }
        }

        std::unordered_set<std::string> field_keys;
        for (const auto& field: declaration_.fields) {
            ValidateFieldDescriptor(field.descriptor);

            if (!field_keys.insert(MemberKey(field.name, field.descriptor)).second) {
                FailBuild("duplicate intrinsic field: " + field.name + ":" + field.descriptor);
            }

            if (declaration_.is_interface && !field.is_static) {
                FailBuild("intrinsic interface has an instance field: " + field.name);
            }

            if (field.has_constant && field.descriptor != "Ljava/lang/String;") {
                ValidateIntegralConstant(field);
            }
        }

        return std::move(declaration_);
    }
} // namespace ogplay::runtime::dexvm
