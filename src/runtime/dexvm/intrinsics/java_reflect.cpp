// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_lang_Class.cpp ----
#include "catalog.h"
#include "shared.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[nodiscard]] DexClassId Represented(IntrinsicContext& context) {
    return context.vm.Model().ClassOfClassObject(context.receiver);
}

[[nodiscard]] bool IsPrimitiveDescriptor(const std::string_view descriptor) {
    return descriptor.size() == 1;
}

[[nodiscard]] std::vector<DexClassId> ParameterTypes(
    IntrinsicContext& context, const VmObjectRef array) {
    std::vector<DexClassId> result;
    if (!array.IsValid()) return result;
    auto& model = context.vm.Model();
    const auto length = model.ArrayLength(array);
    result.reserve(static_cast<std::size_t>(length));
    for (JniSize index = 0; index < length; ++index) {
        const auto item = model.GetObjectElement(array, index);
        if (!item.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NoSuchMethodException;",
                              "parameter type is null"};
        }
        result.push_back(model.ClassOfClassObject(item));
    }
    return result;
}

[[nodiscard]] std::string RequireName(IntrinsicContext& context,
                                      const VmObjectRef name) {
    if (!name.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "name == null"};
    }
    return context.vm.StringUtf8(name);
}

[[nodiscard]] std::optional<DexClassId> ComponentType(
    IntrinsicContext& context, const DexClassId represented) {
    const auto descriptor =
        context.vm.Linker().Class(represented).descriptor;
    if (!descriptor.starts_with("[")) return std::nullopt;
    return context.vm.Linker().ResolveDescriptor(
        std::string_view(descriptor).substr(1));
}

[[nodiscard]] std::vector<DexClassId> DirectInterfaces(
    IntrinsicContext& context, const DexClassId represented) {
    auto& linker = context.vm.Linker();
    const auto& linked = linker.Class(represented);
    if (IsPrimitiveDescriptor(linked.descriptor)) return {};
    if (linked.is_array) {
        return {linker.ResolveDescriptor("Ljava/lang/Cloneable;"),
                linker.ResolveDescriptor("Ljava/io/Serializable;")};
    }
    return linked.direct_interfaces;
}

[[nodiscard]] std::uint32_t ClassModifiers(IntrinsicContext& context,
                                           const DexClassId represented) {
    auto& linker = context.vm.Linker();
    const auto& linked = linker.Class(represented);
    if (IsPrimitiveDescriptor(linked.descriptor)) {
        return kAccPublic | kAccFinal | kAccAbstract;
    }
    if (!linked.is_array) {
        const auto system = linker.ReflectionSystemMetadata(represented);
        return (system.has_inner_class ? system.inner_access_flags
                                       : linked.access_flags) &
               kAccJavaFlagsMask;
    }

    std::string_view leaf = linked.descriptor;
    while (leaf.starts_with("[")) leaf.remove_prefix(1);
    const auto leaf_class = linker.ResolveDescriptor(leaf);
    return ((ClassModifiers(context, leaf_class) & kAccJavaFlagsMask) &
            ~kAccInterface) |
           kAccFinal | kAccAbstract;
}

[[nodiscard]] std::string SimpleName(IntrinsicContext& context,
                                     const DexClassId represented) {
    if (const auto component = ComponentType(context, represented);
        component.has_value()) {
        return SimpleName(context, *component) + "[]";
    }
    const auto system =
        context.vm.Linker().ReflectionSystemMetadata(represented);
    if (system.has_inner_class) return system.inner_name.value_or("");
    const auto name = ClassNameCodec::ClassGetName(
        context.vm.Linker().Class(represented).descriptor);
    const auto separator = name.rfind('.');
    return separator == std::string::npos ? name : name.substr(separator + 1);
}

[[nodiscard]] std::optional<std::string> CanonicalName(
    IntrinsicContext& context, const DexClassId represented) {
    if (const auto component = ComponentType(context, represented);
        component.has_value()) {
        const auto component_name = CanonicalName(context, *component);
        return component_name.has_value()
            ? std::optional<std::string>(*component_name + "[]")
            : std::nullopt;
    }
    const auto system =
        context.vm.Linker().ReflectionSystemMetadata(represented);
    if (system.enclosing_method.has_value() ||
        (system.has_inner_class && !system.inner_name.has_value())) {
        return std::nullopt;
    }
    if (system.enclosing_class.has_value()) {
        const auto enclosing = CanonicalName(context, *system.enclosing_class);
        if (!enclosing.has_value() || !system.inner_name.has_value()) {
            return std::nullopt;
        }
        return *enclosing + "." + *system.inner_name;
    }
    return ClassNameCodec::ClassGetName(
        context.vm.Linker().Class(represented).descriptor);
}

[[nodiscard]] VmObjectRef EnclosingExecutable(IntrinsicContext& context,
                                               const bool constructor) {
    const auto system = context.vm.Linker().ReflectionSystemMetadata(
        Represented(context));
    if (!system.enclosing_method.has_value()) return VmObjectRef{};
    const auto method = *system.enclosing_method;
    const auto& linked = context.vm.Linker().Method(method);
    if ((linked.name == "<init>") != constructor) return VmObjectRef{};
    if (constructor) {
        for (const auto& meta : context.vm.Reflection().DeclaredConstructors(
                 linked.owner)) {
            if (meta.method == method) {
                return context.vm.Reflection().MaterializeConstructor(meta);
            }
        }
    } else {
        for (const auto& meta : context.vm.Reflection().DeclaredMethods(
                 linked.owner)) {
            if (meta.method == method) {
                return context.vm.Reflection().MaterializeMethod(meta);
            }
        }
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "enclosing executable is not reflectable");
}

[[noreturn]] void ThrowNoSuchMethod(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchMethodException;", std::string(name)};
}

[[noreturn]] void ThrowNoSuchField(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchFieldException;", std::string(name)};
}

[[nodiscard]] VmValue SetClassNotFound(IntrinsicContext& context,
                                       const std::string_view name,
                                       const VmObjectRef cause = VmObjectRef{}) {
    const auto wrapper = context.vm.MakeThrowable(
        "Ljava/lang/ClassNotFoundException;", name);
    if (cause.IsValid()) {
        const auto field = context.vm.Linker().FindFieldRecursive(
            context.vm.Model().ObjectClass(wrapper), "ex",
            "Ljava/lang/Throwable;");
        if (!field.has_value()) {
            throw DexVmError(DexVmErrorReason::internal_invariant,
                             "ClassNotFoundException.ex is missing");
        }
        const auto& linked = context.vm.Linker().Field(*field);
        context.vm.Model().InstanceSlots(wrapper)[linked.slot] = {
            cause.Value(), SlotTag::ref};
    }
    context.vm.SetPendingException(wrapper);
    return VmValue::Ref(VmObjectRef{});
}

[[nodiscard]] VmValue ForName(IntrinsicContext& context,
                              const std::string_view name,
                              const bool initialize,
                              const VmClassLoaderId loader) {
    DexClassId java_class;
    try {
        java_class = context.vm.ClassLoaders().LoadClass(loader, name);
    } catch (const ClassNameCodecError&) {
        return SetClassNotFound(context, name);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class) {
            return SetClassNotFound(context, name);
        }
        // API19 dvmFindClassByName retains the lookup/link exception as the
        // cause of ClassNotFoundException. The Java half only unwraps an
        // ExceptionInInitializerError; link failures remain wrapped.
        const auto cause = context.vm.MakeThrowable(
            "Ljava/lang/LinkageError;", error.what());
        return SetClassNotFound(context, name, cause);
    }
    if (initialize) {
        const auto outcome = context.vm.EnsureClassInitialized(java_class);
        if (outcome.exception.IsValid()) {
            context.vm.SetPendingException(outcome.exception);
            return VmValue::Ref(VmObjectRef{});
        }
    }
    return VmValue::Ref(context.vm.Model().ClassObject(java_class));
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_Class() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/Class;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/reflect/AnnotatedElement;",
         "Ljava/lang/reflect/GenericDeclaration;",
         "Ljava/lang/reflect/Type;"},
        kAccPublic | kAccFinal);

    builder.StaticMethod(
        "forName", "(Ljava/lang/String;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto name = context.vm.StringUtf8(
                call.NonNullRef(0, "className"));
            // API19 Class.forName(String) obtains the raw defining loader of
            // the real caller. A bootstrap caller yields null, which the Java
            // three-argument overload normalizes to the system loader.
            auto role = kApplicationLoader;
            if (const auto caller = context.vm.CurrentCallerClass();
                caller.has_value() &&
                context.vm.Linker().Class(*caller).defining_loader !=
                    kBootstrapLoader) {
                role = context.vm.Linker().Class(*caller).defining_loader;
            }
            return ForName(context, name, true, role);
        });
    builder.StaticMethod(
        "forName",
        "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            IntrinsicCall call(context);
            const auto name = context.vm.StringUtf8(
                call.NonNullRef(0, "className"));
            const auto loader_object = context.arguments[2].ref;
            auto role = kApplicationLoader;
            if (loader_object.IsValid()) {
                const auto resolved =
                    context.vm.ClassLoaders().RoleOf(loader_object);
                if (!resolved.has_value()) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "unknown class loader"};
                }
                role = *resolved;
            }
            return ForName(context, name, context.arguments[1].AsInt() != 0,
                           role);
        });

    builder.VirtualMethod("getName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.NewStringUtf8(
                ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(Represented(context))
                        .descriptor)));
        });
    builder.VirtualMethod("getSimpleName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.NewStringUtf8(
                SimpleName(context, Represented(context))));
        });
    builder.VirtualMethod("getCanonicalName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto name = CanonicalName(context, Represented(context));
            return VmValue::Ref(name.has_value()
                ? context.vm.NewStringUtf8(*name)
                : VmObjectRef{});
        });
    builder.VirtualMethod("getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Ref(
                system.enclosing_class.has_value() &&
                    !system.enclosing_method.has_value()
                ? context.vm.Model().ClassObject(*system.enclosing_class)
                : VmObjectRef{});
        });
    builder.VirtualMethod("getEnclosingClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Ref(system.enclosing_class.has_value()
                ? context.vm.Model().ClassObject(*system.enclosing_class)
                : VmObjectRef{});
        });
    builder.VirtualMethod(
        "getEnclosingMethod", "()Ljava/lang/reflect/Method;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(EnclosingExecutable(context, false));
        });
    builder.VirtualMethod(
        "getEnclosingConstructor", "()Ljava/lang/reflect/Constructor;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(EnclosingExecutable(context, true));
        });
    builder.VirtualMethod("isAnonymousClass", "()Z",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Int(system.has_inner_class &&
                                    !system.inner_name.has_value()
                ? 1 : 0);
        });
    builder.VirtualMethod("isLocalClass", "()Z",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Int(system.enclosing_method.has_value() &&
                                    system.inner_name.has_value()
                ? 1 : 0);
        });
    builder.VirtualMethod("isMemberClass", "()Z",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Int(system.enclosing_class.has_value() &&
                                    !system.enclosing_method.has_value()
                ? 1 : 0);
        });
    builder.VirtualMethod("getDeclaredClasses", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto system = context.vm.Linker().ReflectionSystemMetadata(
                Represented(context));
            return VmValue::Ref(context.vm.Reflection().MaterializeTypeArray(
                system.member_classes));
        });
    builder.VirtualMethod("getClassLoader", "()Ljava/lang/ClassLoader;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(
                context.vm.ClassLoaders().LoaderForClass(Represented(context)));
        });
    builder.VirtualMethod("getComponentType", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto component = ComponentType(context, Represented(context));
            return VmValue::Ref(component.has_value()
                ? context.vm.Model().ClassObject(*component)
                : VmObjectRef{});
        });
    builder.VirtualMethod("getSuperclass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto represented = Represented(context);
            const auto& linked = context.vm.Linker().Class(represented);
            if (IsPrimitiveDescriptor(linked.descriptor) || linked.is_interface ||
                !linked.super.has_value()) {
                return VmValue::Ref(VmObjectRef{});
            }
            return VmValue::Ref(
                context.vm.Model().ClassObject(*linked.super));
        });
    builder.VirtualMethod("getInterfaces", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto interfaces = DirectInterfaces(context, Represented(context));
            return VmValue::Ref(
                context.vm.Reflection().MaterializeTypeArray(interfaces));
        });
    builder.VirtualMethod("getModifiers", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            ClassModifiers(context, Represented(context))));
    });
    builder.VirtualMethod("isArray", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            context.vm.Linker().Class(Represented(context)).is_array ? 1 : 0);
    });
    builder.VirtualMethod("isInterface", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            context.vm.Linker().Class(Represented(context)).is_interface ? 1 : 0);
    });
    builder.VirtualMethod("isPrimitive", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(IsPrimitiveDescriptor(
            context.vm.Linker().Class(Represented(context)).descriptor) ? 1 : 0);
    });
    builder.VirtualMethod("isEnum", "()Z", [](IntrinsicContext& context) {
        const auto represented = Represented(context);
        const auto& linked = context.vm.Linker().Class(represented);
        const auto enum_class = context.vm.Linker().ResolveDescriptor(
            "Ljava/lang/Enum;");
        return VmValue::Int(linked.super == enum_class &&
            (ClassModifiers(context, represented) & kAccEnum) != 0U ? 1 : 0);
    });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            (ClassModifiers(context, Represented(context)) & kAccSynthetic) != 0U
                ? 1 : 0);
    });
    builder.VirtualMethod("isInstance", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            const auto object = context.arguments[0].ref;
            return VmValue::Int(object.IsValid() &&
                context.vm.Linker().IsAssignable(
                    Represented(context), context.vm.Model().ObjectClass(object))
                    ? 1 : 0);
        });
    builder.VirtualMethod("isAssignableFrom", "(Ljava/lang/Class;)Z",
        [](IntrinsicContext& context) {
            const auto candidate = context.arguments[0].ref;
            if (!candidate.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "cls == null"};
            }
            return VmValue::Int(context.vm.Linker().IsAssignable(
                Represented(context),
                context.vm.Model().ClassOfClassObject(candidate)) ? 1 : 0);
        });
    builder.VirtualMethod("cast", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            const auto object = context.arguments[0].ref;
            if (!object.IsValid()) return VmValue::Ref(VmObjectRef{});
            const auto desired = Represented(context);
            const auto actual = context.vm.Model().ObjectClass(object);
            if (context.vm.Linker().IsAssignable(desired, actual)) {
                return VmValue::Ref(object);
            }
            throw VmJavaThrow{
                "Ljava/lang/ClassCastException;",
                ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(actual).descriptor) +
                    " cannot be cast to " + ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(desired).descriptor)};
        });
    builder.VirtualMethod("asSubclass", "(Ljava/lang/Class;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto desired_object = context.arguments[0].ref;
            if (!desired_object.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "clazz == null"};
            }
            const auto represented = Represented(context);
            const auto desired =
                context.vm.Model().ClassOfClassObject(desired_object);
            if (context.vm.Linker().IsAssignable(desired, represented)) {
                return VmValue::Ref(context.receiver);
            }
            throw VmJavaThrow{
                "Ljava/lang/ClassCastException;",
                ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(represented).descriptor) +
                    " cannot be cast to " + ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(desired).descriptor)};
        });
    builder.OverrideMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto represented = Represented(context);
            const auto& linked = context.vm.Linker().Class(represented);
            const auto text = IsPrimitiveDescriptor(linked.descriptor)
                ? SimpleName(context, represented)
                : std::string(linked.is_interface ? "interface " : "class ") +
                      ClassNameCodec::ClassGetName(linked.descriptor);
            return VmValue::Ref(context.vm.NewStringUtf8(text));
        });
    builder.VirtualMethod("newInstance", "()Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().NewInstance(
                Represented(context), context.vm.CurrentCallerClass()));
        });

    builder.VirtualMethod("getDeclaredMethods", "()[Ljava/lang/reflect/Method;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().MaterializeDeclaredMethods(
                Represented(context)));
        });
    builder.VirtualMethod("getMethods", "()[Ljava/lang/reflect/Method;",
        [](IntrinsicContext& context) {
            const auto methods =
                context.vm.Reflection().PublicMethods(Represented(context));
            return VmValue::Ref(
                context.vm.Reflection().MaterializeMethods(methods));
        });
    builder.VirtualMethod(
        "getDeclaredMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        [](IntrinsicContext& context) {
            const auto name = RequireName(context, context.arguments[0].ref);
            const auto parameters =
                ParameterTypes(context, context.arguments[1].ref);
            const auto method = context.vm.Reflection().FindDeclaredMethod(
                Represented(context), name, parameters);
            if (!method.has_value()) ThrowNoSuchMethod(name);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeMethod(*method));
        });
    builder.VirtualMethod(
        "getMethod",
        "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
        [](IntrinsicContext& context) {
            const auto name = RequireName(context, context.arguments[0].ref);
            const auto parameters =
                ParameterTypes(context, context.arguments[1].ref);
            const auto method = context.vm.Reflection().FindPublicMethod(
                Represented(context), name, parameters);
            if (!method.has_value()) ThrowNoSuchMethod(name);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeMethod(*method));
        });

    builder.VirtualMethod(
        "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().MaterializeConstructors(
                context.vm.Reflection().DeclaredConstructors(
                    Represented(context))));
        });
    builder.VirtualMethod("getConstructors", "()[Ljava/lang/reflect/Constructor;",
        [](IntrinsicContext& context) {
            std::vector<ReflectConstructorMeta> constructors;
            for (const auto& constructor :
                 context.vm.Reflection().DeclaredConstructors(
                     Represented(context))) {
                if ((constructor.access_flags & kAccPublic) != 0U) {
                    constructors.push_back(constructor);
                }
            }
            return VmValue::Ref(
                context.vm.Reflection().MaterializeConstructors(constructors));
        });
    const auto add_constructor_lookup = [&](const std::string& name,
                                            const bool public_only) {
        builder.VirtualMethod(
            name,
            "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;",
            [public_only](IntrinsicContext& context) {
                const auto parameters =
                    ParameterTypes(context, context.arguments[0].ref);
                const auto constructor = context.vm.Reflection().FindConstructor(
                    Represented(context), parameters, public_only);
                if (!constructor.has_value()) ThrowNoSuchMethod("<init>");
                return VmValue::Ref(
                    context.vm.Reflection().MaterializeConstructor(*constructor));
            });
    };
    add_constructor_lookup("getDeclaredConstructor", false);
    add_constructor_lookup("getConstructor", true);

    builder.VirtualMethod("getDeclaredFields", "()[Ljava/lang/reflect/Field;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().MaterializeFields(
                context.vm.Reflection().DeclaredFields(Represented(context))));
        });
    builder.VirtualMethod("getFields", "()[Ljava/lang/reflect/Field;",
        [](IntrinsicContext& context) {
            const auto fields =
                context.vm.Reflection().PublicFields(Represented(context));
            return VmValue::Ref(
                context.vm.Reflection().MaterializeFields(fields));
        });
    const auto add_field_lookup = [&](const std::string& method_name,
                                      const bool public_only) {
        builder.VirtualMethod(
            method_name,
            "(Ljava/lang/String;)Ljava/lang/reflect/Field;",
            [public_only](IntrinsicContext& context) {
                const auto name = RequireName(context, context.arguments[0].ref);
                const auto field = public_only
                    ? context.vm.Reflection().FindPublicField(
                          Represented(context), name)
                    : context.vm.Reflection().FindDeclaredField(
                          Represented(context), name);
                if (!field.has_value()) ThrowNoSuchField(name);
                return VmValue::Ref(
                    context.vm.Reflection().MaterializeField(*field));
            });
    };
    add_field_lookup("getDeclaredField", false);
    add_field_lookup("getField", true);

    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_AccessibleObject.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/AccessibleObject;", "Ljava/lang/Object;",
        {"Ljava/lang/reflect/AnnotatedElement;"});
    builder.InstanceField("flag", "Z", kAccNone);
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    }, kAccProtected);
    builder.StaticMethod(
        "setAccessible", "([Ljava/lang/reflect/AccessibleObject;Z)V",
        [](IntrinsicContext& context) {
            const auto objects = context.arguments[0].ref;
            if (!objects.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "objects == null"};
            }
            const auto accessible = context.arguments[1].AsInt() != 0;
            const auto length = context.vm.Model().ArrayLength(objects);
            for (JniSize index = 0; index < length; ++index) {
                const auto object =
                    context.vm.Model().GetObjectElement(objects, index);
                if (!object.IsValid()) {
                    throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                      "objects contains null"};
                }
                context.vm.Reflection().SetAccessible(object, accessible);
            }
            return VmValue::Void();
        });
    builder.VirtualMethod("isAccessible", "()Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(
                context.vm.Reflection().IsAccessible(context.receiver) ? 1 : 0);
        });
    builder.VirtualMethod("setAccessible", "(Z)V",
        [](IntrinsicContext& context) {
            context.vm.Reflection().SetAccessible(
                context.receiver, context.arguments[0].AsInt() != 0);
            return VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_AnnotatedElement.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_AnnotatedElement() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/AnnotatedElement;");
    builder.UnimplementedVirtual(
        "isAnnotationPresent", "(Ljava/lang/Class;)Z");
    builder.UnimplementedVirtual(
        "getAnnotation", "(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;");
    builder.UnimplementedVirtual(
        "getAnnotations", "()[Ljava/lang/annotation/Annotation;");
    builder.UnimplementedVirtual(
        "getDeclaredAnnotations", "()[Ljava/lang/annotation/Annotation;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Array.cpp ----
#include "catalog.h"
#include "shared.h"

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/reflection.h"
#include "ogplay/runtime/dexvm/reflection_codec.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
using namespace detail;

[[nodiscard]] bool IsArrayKind(const VmObjectKind kind) {
    return kind == VmObjectKind::primitive_array ||
           kind == VmObjectKind::object_array;
}

[[nodiscard]] VmObjectKind RequireArray(JavaObjectModel& model,
                                        const VmObjectRef array) {
    if (!array.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "array == null"};
    }
    const auto kind = model.Kind(array);
    if (!IsArrayKind(kind)) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "object is not an array"};
    }
    return kind;
}

void CheckIndex(JavaObjectModel& model, const VmObjectRef array,
                const std::int32_t index) {
    const auto length = model.ArrayLength(array);
    if (index < 0 || index >= length) {
        throw VmJavaThrow{"Ljava/lang/ArrayIndexOutOfBoundsException;",
                          std::to_string(index)};
    }
}

[[nodiscard]] std::string PrimitiveDescriptor(const JniPrimitiveKind kind) {
    switch (kind) {
        case JniPrimitiveKind::boolean: return "Z";
        case JniPrimitiveKind::byte: return "B";
        case JniPrimitiveKind::character: return "C";
        case JniPrimitiveKind::short_integer: return "S";
        case JniPrimitiveKind::integer: return "I";
        case JniPrimitiveKind::long_integer: return "J";
        case JniPrimitiveKind::float_value: return "F";
        case JniPrimitiveKind::double_value: return "D";
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "unknown primitive array kind");
}

[[nodiscard]] VmValue PrimitiveValue(JavaObjectModel& model,
                                     const VmObjectRef array,
                                     const std::int32_t index) {
    const auto kind = model.PrimitiveArrayKind(array);
    const auto bits = model.GetPrimitiveElement(array, index);
    switch (kind) {
        case JniPrimitiveKind::boolean:
            return VmValue::Int(static_cast<std::int32_t>(bits & 1U));
        case JniPrimitiveKind::byte:
            return VmValue::Int(static_cast<std::int8_t>(bits));
        case JniPrimitiveKind::character:
            return VmValue::Int(static_cast<std::uint16_t>(bits));
        case JniPrimitiveKind::short_integer:
            return VmValue::Int(static_cast<std::int16_t>(bits));
        case JniPrimitiveKind::integer:
            return VmValue::Int(static_cast<std::int32_t>(bits));
        case JniPrimitiveKind::long_integer:
            return VmValue::Long(static_cast<std::int64_t>(bits));
        case JniPrimitiveKind::float_value:
            return VmValue::Float(
                std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
        case JniPrimitiveKind::double_value:
            return VmValue::Double(std::bit_cast<double>(bits));
    }
    throw DexVmError(DexVmErrorReason::internal_invariant,
                     "unknown primitive array kind");
}

[[nodiscard]] std::uint64_t PrimitiveBits(const VmValue& value,
                                          const std::string_view descriptor) {
    if (descriptor == "J" || descriptor == "D") return value.wide;
    return value.cat1;
}

[[nodiscard]] VmObjectRef NewArray(IntrinsicContext& context,
                                   const VmObjectRef class_object,
                                   const std::vector<std::int32_t>& dims) {
    if (!class_object.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                          "componentType == null"};
    }
    const auto element_class =
        context.vm.Model().ClassOfClassObject(class_object);
    const auto element_descriptor =
        context.vm.Linker().Class(element_class).descriptor;
    if (element_descriptor == "V") {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                          "cannot allocate an array of void"};
    }
    for (const auto length : dims) {
        if (length < 0) {
            throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;",
                              std::to_string(length)};
        }
    }
    return BuildReflectArray(context.vm, element_descriptor, dims);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_reflect_Array() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Array;", "Ljava/lang/Object;", {},
        kAccPublic | kAccFinal);
    builder.StaticMethod(
        "newInstance", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(NewArray(
                context, context.arguments[0].ref,
                {context.arguments[1].AsInt()}));
        });
    builder.StaticMethod(
        "newInstance", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto dims_ref = context.arguments[1].ref;
            if (!dims_ref.IsValid()) {
                throw VmJavaThrow{"Ljava/lang/NullPointerException;",
                                  "dimensions == null"};
            }
            if (model.Kind(dims_ref) != VmObjectKind::primitive_array ||
                model.PrimitiveArrayKind(dims_ref) !=
                    JniPrimitiveKind::integer) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "dimensions is not int[]"};
            }
            const auto count = model.ArrayLength(dims_ref);
            if (count <= 0 || count > 255) {
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                  "invalid dimension count"};
            }
            std::vector<std::int32_t> dims;
            dims.reserve(static_cast<std::size_t>(count));
            for (JniSize index = 0; index < count; ++index) {
                dims.push_back(static_cast<std::int32_t>(
                    model.GetPrimitiveElement(dims_ref, index)));
            }
            return VmValue::Ref(
                NewArray(context, context.arguments[0].ref, dims));
        });
    builder.StaticMethod("getLength", "(Ljava/lang/Object;)I",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            static_cast<void>(RequireArray(model, context.arguments[0].ref));
            return VmValue::Int(model.ArrayLength(context.arguments[0].ref));
        });
    builder.StaticMethod(
        "get", "(Ljava/lang/Object;I)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto array = context.arguments[0].ref;
            const auto index = context.arguments[1].AsInt();
            const auto kind = RequireArray(model, array);
            CheckIndex(model, array, index);
            if (kind == VmObjectKind::object_array) {
                return VmValue::Ref(model.GetObjectElement(array, index));
            }
            const auto type = context.vm.Linker().ResolveDescriptor(
                PrimitiveDescriptor(model.PrimitiveArrayKind(array)));
            return VmValue::Ref(context.vm.Reflection().Codec().BoxReturn(
                type, PrimitiveValue(model, array, index)));
        });
    builder.StaticMethod(
        "set", "(Ljava/lang/Object;ILjava/lang/Object;)V",
        [](IntrinsicContext& context) {
            auto& model = context.vm.Model();
            const auto array = context.arguments[0].ref;
            const auto index = context.arguments[1].AsInt();
            const auto kind = RequireArray(model, array);
            CheckIndex(model, array, index);
            if (kind == VmObjectKind::object_array) {
                const auto converted = context.vm.Reflection().Codec()
                    .ConvertArgument(context.arguments[2].ref,
                                     model.ObjectArrayElementClass(array));
                model.SetObjectElement(array, index, converted.ref);
                return VmValue::Void();
            }
            const auto descriptor =
                PrimitiveDescriptor(model.PrimitiveArrayKind(array));
            const auto type = context.vm.Linker().ResolveDescriptor(descriptor);
            const auto converted = context.vm.Reflection().Codec()
                .ConvertArgument(context.arguments[2].ref, type);
            model.SetPrimitiveElement(
                array, index, PrimitiveBits(converted, descriptor));
            return VmValue::Void();
        });

    struct PrimitiveMethod final {
        const char* suffix;
        const char* descriptor;
    };
    constexpr PrimitiveMethod primitives[]{
        {"Boolean", "Z"}, {"Byte", "B"}, {"Char", "C"},
        {"Short", "S"},   {"Int", "I"},  {"Long", "J"},
        {"Float", "F"},   {"Double", "D"},
    };
    for (const auto& primitive : primitives) {
        const std::string requested(primitive.descriptor);
        builder.StaticMethod(
            std::string("get") + primitive.suffix,
            std::string("(Ljava/lang/Object;I)") + requested,
            [requested](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array = context.arguments[0].ref;
                const auto index = context.arguments[1].AsInt();
                if (RequireArray(model, array) !=
                    VmObjectKind::primitive_array) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "array has incompatible type"};
                }
                CheckIndex(model, array, index);
                const auto source_descriptor =
                    PrimitiveDescriptor(model.PrimitiveArrayKind(array));
                const auto source = context.vm.Linker().ResolveDescriptor(
                    source_descriptor);
                const auto target =
                    context.vm.Linker().ResolveDescriptor(requested);
                const auto boxed = context.vm.Reflection().Codec().BoxReturn(
                    source, PrimitiveValue(model, array, index));
                return context.vm.Reflection().Codec().ConvertArgument(
                    boxed, target);
            });
        builder.StaticMethod(
            std::string("set") + primitive.suffix,
            std::string("(Ljava/lang/Object;I") + requested + ")V",
            [requested](IntrinsicContext& context) {
                auto& model = context.vm.Model();
                const auto array = context.arguments[0].ref;
                const auto index = context.arguments[1].AsInt();
                if (RequireArray(model, array) !=
                    VmObjectKind::primitive_array) {
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;",
                                      "array has incompatible type"};
                }
                CheckIndex(model, array, index);
                const auto target_descriptor =
                    PrimitiveDescriptor(model.PrimitiveArrayKind(array));
                const auto source =
                    context.vm.Linker().ResolveDescriptor(requested);
                const auto target = context.vm.Linker().ResolveDescriptor(
                    target_descriptor);
                const auto boxed = context.vm.Reflection().Codec().BoxReturn(
                    source, context.arguments[2]);
                const auto converted = context.vm.Reflection().Codec()
                    .ConvertArgument(boxed, target);
                model.SetPrimitiveElement(
                    array, index,
                    PrimitiveBits(converted, target_descriptor));
                return VmValue::Void();
            });
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Constructor.cpp ----
#include "catalog.h"
#include "shared.h"

#include <cstdint>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Constructor() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Constructor;",
        "Ljava/lang/reflect/AccessibleObject;",
        {"Ljava/lang/reflect/GenericDeclaration;",
         "Ljava/lang/reflect/Member;"}, kAccPublic | kAccFinal);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", kAccNone);
    builder.InstanceField("parameterTypes", "[Ljava/lang/Class;", kAccNone);
    builder.InstanceField("exceptionTypes", "[Ljava/lang/Class;", kAccNone);
    builder.InstanceField("slot", "I", kAccNone);
    builder.InstanceField("methodDexIndex", "I", kAccPrivate);

    builder.VirtualMethod("getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().ClassObject(
                context.vm.Reflection().ConstructorMetadata(context.receiver)
                    .declaring_class));
        });
    builder.VirtualMethod("getName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().ConstructorMetadata(
                context.receiver);
            return VmValue::Ref(context.vm.NewStringUtf8(
                ClassNameCodec::ClassGetName(
                    context.vm.Linker().Class(meta.declaring_class)
                        .descriptor)));
        });
    builder.VirtualMethod("getModifiers", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.Reflection().ConstructorMetadata(context.receiver)
                .access_flags & kJavaConstructorModifierMask));
    });
    builder.VirtualMethod("getParameterTypes", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().ConstructorMetadata(
                context.receiver);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeTypeArray(
                    meta.parameter_types));
        });
    builder.VirtualMethod("getExceptionTypes", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().ConstructorMetadata(
                context.receiver);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeTypeArray(
                    meta.exception_types));
        });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            (context.vm.Reflection().ConstructorMetadata(context.receiver)
                 .access_flags & kAccSynthetic) != 0U ? 1 : 0);
    });
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
        });
    builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext& context) {
        const auto& meta = context.vm.Reflection().ConstructorMetadata(
            context.receiver);
        return VmValue::Int(detail::JavaUtf8Hash(
            context, ClassNameCodec::ClassGetName(
                context.vm.Linker().Class(meta.declaring_class).descriptor)));
    });
    builder.OverrideMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().ConstructorMetadata(
                context.receiver);
            auto text = detail::ModifierString(
                meta.access_flags & kJavaConstructorModifierMask);
            if (!text.empty()) text.push_back(' ');
            text += ClassNameCodec::ClassGetName(
                context.vm.Linker().Class(meta.declaring_class).descriptor);
            text.push_back('(');
            text += detail::PrintableTypeList(context, meta.parameter_types);
            text.push_back(')');
            if (!meta.exception_types.empty()) {
                text += " throws ";
                text += detail::PrintableTypeList(context,
                                                  meta.exception_types);
            }
            return VmValue::Ref(context.vm.NewStringUtf8(text));
        });
    builder.VirtualMethod(
        "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().InvokeConstructor(
                context.receiver, context.arguments[0].ref,
                context.vm.CurrentCallerClass()));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Field.cpp ----
#include "catalog.h"
#include "shared.h"

#include <cstdint>
#include <optional>
#include <string>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Field() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Field;", "Ljava/lang/reflect/AccessibleObject;",
        {"Ljava/lang/reflect/Member;"}, kAccPublic | kAccFinal);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", kAccPrivate);
    builder.InstanceField("type", "Ljava/lang/Class;", kAccPrivate);
    builder.InstanceField("name", "Ljava/lang/String;", kAccPrivate);
    builder.InstanceField("slot", "I", kAccPrivate);
    builder.InstanceField("fieldDexIndex", "I", kAccPrivate | kAccFinal);

    builder.VirtualMethod("getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().ClassObject(
                context.vm.Reflection().FieldMetadata(context.receiver)
                    .declaring_class));
        });
    builder.VirtualMethod("getName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta =
                context.vm.Reflection().FieldMetadata(context.receiver);
            return VmValue::Ref(context.vm.NewStringUtf8(
                context.vm.Linker().Field(meta.field).name));
        });
    builder.VirtualMethod("getType", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().ClassObject(
                context.vm.Reflection().FieldMetadata(context.receiver).type));
        });
    builder.VirtualMethod("getModifiers", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.Reflection().FieldMetadata(context.receiver)
                .access_flags & kJavaFieldModifierMask));
    });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            (context.vm.Reflection().FieldMetadata(context.receiver)
                 .access_flags & kAccSynthetic) != 0U ? 1 : 0);
    });
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
        });
    builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext& context) {
        const auto& meta = context.vm.Reflection().FieldMetadata(
            context.receiver);
        const auto& linker = context.vm.Linker();
        return VmValue::Int(
            detail::JavaUtf8Hash(context, linker.Field(meta.field).name) ^
            detail::JavaUtf8Hash(
                context, ClassNameCodec::ClassGetName(
                    linker.Class(meta.declaring_class).descriptor)));
    });
    builder.OverrideMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().FieldMetadata(
                context.receiver);
            const auto& linker = context.vm.Linker();
            auto text = detail::ModifierString(
                meta.access_flags & kJavaFieldModifierMask);
            if (!text.empty()) text.push_back(' ');
            text += detail::PrintableTypeName(context, meta.type);
            text.push_back(' ');
            text += detail::PrintableTypeName(context, meta.declaring_class);
            text.push_back('.');
            text += linker.Field(meta.field).name;
            return VmValue::Ref(context.vm.NewStringUtf8(text));
        });
    builder.VirtualMethod(
        "get", "(Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return context.vm.Reflection().GetField(
                context.receiver, context.arguments[0].ref, std::nullopt,
                context.vm.CurrentCallerClass());
        });
    builder.VirtualMethod(
        "set", "(Ljava/lang/Object;Ljava/lang/Object;)V",
        [](IntrinsicContext& context) {
            context.vm.Reflection().SetField(
                context.receiver, context.arguments[0].ref,
                VmValue::Ref(context.arguments[1].ref), std::nullopt,
                context.vm.CurrentCallerClass());
            return VmValue::Void();
        });
    struct PrimitiveMethod final {
        const char* suffix;
        const char* descriptor;
    };
    constexpr PrimitiveMethod primitives[]{
        {"Boolean", "Z"}, {"Byte", "B"}, {"Char", "C"},
        {"Short", "S"},   {"Int", "I"},  {"Long", "J"},
        {"Float", "F"},   {"Double", "D"},
    };
    for (const auto& primitive : primitives) {
        const std::string type(primitive.descriptor);
        builder.VirtualMethod(
            std::string("get") + primitive.suffix,
            std::string("(Ljava/lang/Object;)") + type,
            [type](IntrinsicContext& context) {
                return context.vm.Reflection().GetField(
                    context.receiver, context.arguments[0].ref,
                    context.vm.Linker().ResolveDescriptor(type),
                    context.vm.CurrentCallerClass());
            });
        builder.VirtualMethod(
            std::string("set") + primitive.suffix,
            std::string("(Ljava/lang/Object;") + type + ")V",
            [type](IntrinsicContext& context) {
                const auto source =
                    context.vm.Linker().ResolveDescriptor(type);
                context.vm.Reflection().SetField(
                    context.receiver, context.arguments[0].ref,
                    context.arguments[1], source,
                    context.vm.CurrentCallerClass());
                return VmValue::Void();
            });
    }
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_GenericDeclaration.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_GenericDeclaration() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/GenericDeclaration;",
        {"Ljava/lang/reflect/AnnotatedElement;"});
    builder.UnimplementedVirtual(
        "getTypeParameters", "()[Ljava/lang/reflect/TypeVariable;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_InvocationTargetException.cpp ----
#include "catalog.h"

#include <string>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

void SetTarget(IntrinsicContext& context, const VmObjectRef target) {
    const auto field = context.vm.Linker().FindFieldRecursive(
        context.vm.Model().ObjectClass(context.receiver), "target",
        "Ljava/lang/Throwable;");
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "InvocationTargetException.target is missing");
    }
    const auto& linked = context.vm.Linker().Field(*field);
    context.vm.Model().InstanceSlots(context.receiver)[linked.slot] = {
        target.Value(), SlotTag::ref};
}

[[nodiscard]] VmObjectRef Target(IntrinsicContext& context) {
    const auto field = context.vm.Linker().FindFieldRecursive(
        context.vm.Model().ObjectClass(context.receiver), "target",
        "Ljava/lang/Throwable;");
    if (!field.has_value()) {
        throw DexVmError(DexVmErrorReason::internal_invariant,
                         "InvocationTargetException.target is missing");
    }
    return VmObjectRef(context.vm.Model()
                           .InstanceSlots(context.receiver)
                               [context.vm.Linker().Field(*field).slot]
                           .bits);
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_reflect_InvocationTargetException() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/InvocationTargetException;",
        "Ljava/lang/ReflectiveOperationException;");
    builder.InstanceField("target", "Ljava/lang/Throwable;", kAccPrivate);
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    }, kAccProtected);
    builder.Constructor("(Ljava/lang/Throwable;)V",
        [](IntrinsicContext& context) {
            SetTarget(context, context.arguments[0].ref);
            return VmValue::Void();
        });
    builder.Constructor(
        "(Ljava/lang/Throwable;Ljava/lang/String;)V",
        [](IntrinsicContext& context) {
            SetTarget(context, context.arguments[0].ref);
            context.vm.SetThrowableMessage(context.receiver,
                                           context.arguments[1].ref);
            return VmValue::Void();
        });
    builder.VirtualMethod("getTargetException", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(Target(context));
        });
    builder.VirtualMethod("getCause", "()Ljava/lang/Throwable;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(Target(context));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Member.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Member() {
    auto builder = IntrinsicClassBuilder::Interface(
        "Ljava/lang/reflect/Member;");
    builder.UnimplementedVirtual("getDeclaringClass", "()Ljava/lang/Class;");
    builder.UnimplementedVirtual("getName", "()Ljava/lang/String;");
    builder.UnimplementedVirtual("getModifiers", "()I");
    builder.UnimplementedVirtual("isSynthetic", "()Z");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Method.cpp ----
#include "catalog.h"
#include "shared.h"

#include <cstdint>
#include <string_view>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {
IntrinsicClassDecl Declare_java_lang_reflect_Method() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Method;", "Ljava/lang/reflect/AccessibleObject;",
        {"Ljava/lang/reflect/GenericDeclaration;",
         "Ljava/lang/reflect/Member;"}, kAccPublic | kAccFinal);
    builder.InstanceField("slot", "I", kAccPrivate);
    builder.InstanceField("methodDexIndex", "I", kAccPrivate | kAccFinal);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", kAccPrivate);
    builder.InstanceField("name", "Ljava/lang/String;", kAccPrivate);
    builder.InstanceField("parameterTypes", "[Ljava/lang/Class;", kAccPrivate);
    builder.InstanceField("exceptionTypes", "[Ljava/lang/Class;", kAccPrivate);
    builder.InstanceField("returnType", "Ljava/lang/Class;", kAccPrivate);

    builder.VirtualMethod("getName", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().MethodMetadata(
                context.receiver);
            return VmValue::Ref(context.vm.NewStringUtf8(
                context.vm.Linker().Method(meta.method).name));
        });
    builder.VirtualMethod("getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().ClassObject(
                context.vm.Reflection().MethodMetadata(context.receiver)
                    .declaring_class));
        });
    builder.VirtualMethod("getModifiers", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(
            context.vm.Reflection().MethodMetadata(context.receiver)
                .access_flags & kJavaMethodModifierMask));
    });
    builder.VirtualMethod("getReturnType", "()Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Model().ClassObject(
                context.vm.Reflection().MethodMetadata(context.receiver)
                    .return_type));
        });
    builder.VirtualMethod("getParameterTypes", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().MethodMetadata(
                context.receiver);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeTypeArray(
                    meta.parameter_types));
        });
    builder.VirtualMethod("getExceptionTypes", "()[Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().MethodMetadata(
                context.receiver);
            return VmValue::Ref(
                context.vm.Reflection().MaterializeTypeArray(
                    meta.exception_types));
        });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            (context.vm.Reflection().MethodMetadata(context.receiver)
                 .access_flags & kAccSynthetic) != 0U ? 1 : 0);
    });
    builder.OverrideMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
        });
    builder.OverrideMethod("hashCode", "()I", [](IntrinsicContext& context) {
        const auto& meta = context.vm.Reflection().MethodMetadata(
            context.receiver);
        const auto& method = context.vm.Linker().Method(meta.method);
        return VmValue::Int(detail::JavaUtf8Hash(context, method.name));
    });
    builder.OverrideMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().MethodMetadata(
                context.receiver);
            const auto& linker = context.vm.Linker();
            const auto& method = linker.Method(meta.method);
            auto text = detail::ModifierString(meta.access_flags &
                                               kJavaMethodModifierMask);
            if (!text.empty()) text.push_back(' ');
            text += ClassNameCodec::ClassGetName(
                linker.Class(meta.return_type).descriptor);
            text.push_back(' ');
            text += ClassNameCodec::ClassGetName(
                linker.Class(meta.declaring_class).descriptor);
            text.push_back('.');
            text += method.name;
            text.push_back('(');
            text += detail::PrintableTypeList(context, meta.parameter_types);
            text.push_back(')');
            if (!meta.exception_types.empty()) {
                text += " throws ";
                text += detail::PrintableTypeList(context,
                                                  meta.exception_types);
            }
            return VmValue::Ref(context.vm.NewStringUtf8(text));
    });
    builder.VirtualMethod(
        "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Reflection().InvokeMethod(
                context.receiver, context.arguments[0].ref,
                context.arguments[1].ref,
                context.vm.CurrentCallerClass()));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Modifier.cpp ----
#include "catalog.h"
#include "shared.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Modifier() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Modifier;", "Ljava/lang/Object;");
    builder.StaticMethod("toString", "(I)Ljava/lang/String;",
        [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.NewStringUtf8(
                detail::ModifierString(
                    static_cast<std::uint32_t>(context.arguments[0].AsInt()))));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics


// ---- migrated from java_lang_reflect_Type.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Type() {
    return std::move(IntrinsicClassBuilder::Interface(
                         "Ljava/lang/reflect/Type;"))
        .Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
