// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_lang_Class.cpp ----
#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/runtime/dexvm/class_loader_facade.h"
#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/annotation_runtime.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/proxy_runtime.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[nodiscard]] DexClassId Represented(IntrinsicContext& context) {
    return context.vm.Model().ClassOfClassObject(context.receiver);
}

[[nodiscard]] bool IsPrimitiveDescriptor(const std::string_view descriptor) {
    return descriptor.size() == 1;
}

[[nodiscard]] std::vector<DexClassId> ParameterTypes(IntrinsicContext& context, const VmObjectRef array) {
    std::vector<DexClassId> result;
    if (!array.IsValid()) return result;
    auto& model = context.vm.Model();
    const auto length = model.ArrayLength(array);
    result.reserve(static_cast<std::size_t>(length));
    for (JniSize index = 0; index < length; ++index) {
        const auto item = model.GetObjectElement(array, index);
        if (!item.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NoSuchMethodException;", "parameter type is null"};
        }
        result.push_back(model.ClassOfClassObject(item));
    }
    return result;
}

[[nodiscard]] std::string RequireName(IntrinsicContext& context, const VmObjectRef name) {
    if (!name.IsValid()) {
        throw VmJavaThrow{"Ljava/lang/NullPointerException;", "name == null"};
    }
    return context.vm.StringUtf8(name);
}

[[nodiscard]] std::optional<DexClassId> ComponentType(IntrinsicContext& context, const DexClassId represented) {
    const auto descriptor = context.vm.Linker().Class(represented).descriptor;
    if (!descriptor.starts_with("[")) return std::nullopt;
    return context.vm.Linker().ResolveDescriptor(std::string_view(descriptor).substr(1));
}

[[nodiscard]] std::vector<DexClassId> DirectInterfaces(IntrinsicContext& context, const DexClassId represented) {
    auto& linker = context.vm.Linker();
    const auto& linked = linker.Class(represented);
    if (IsPrimitiveDescriptor(linked.descriptor)) return {};
    if (linked.is_array) {
        return {linker.ResolveDescriptor("Ljava/lang/Cloneable;"), linker.ResolveDescriptor("Ljava/io/Serializable;")};
    }
    return linked.direct_interfaces;
}

[[nodiscard]] std::uint32_t ClassModifiers(IntrinsicContext& context, const DexClassId represented) {
    auto& linker = context.vm.Linker();
    const auto& linked = linker.Class(represented);
    if (IsPrimitiveDescriptor(linked.descriptor)) {
        return kAccPublic | kAccFinal | kAccAbstract;
    }
    if (!linked.is_array) {
        const auto system = linker.ReflectionSystemMetadata(represented);
        return (system.has_inner_class ? system.inner_access_flags : linked.access_flags) & kAccJavaFlagsMask;
    }

    std::string_view leaf = linked.descriptor;
    while (leaf.starts_with("["))
        leaf.remove_prefix(1);
    const auto leaf_class = linker.ResolveDescriptor(leaf);
    return ((ClassModifiers(context, leaf_class) & kAccJavaFlagsMask) & ~kAccInterface) | kAccFinal | kAccAbstract;
}

[[nodiscard]] bool IsEnumClass(IntrinsicContext& context, const DexClassId represented) {
    const auto& linked = context.vm.Linker().Class(represented);
    const auto enum_class = context.vm.Linker().ResolveDescriptor("Ljava/lang/Enum;");
    return linked.super == enum_class && (ClassModifiers(context, represented) & kAccEnum) != 0U;
}

[[nodiscard]] std::string SimpleName(IntrinsicContext& context, const DexClassId represented) {
    if (const auto component = ComponentType(context, represented); component.has_value()) {
        return SimpleName(context, *component) + "[]";
    }
    const auto system = context.vm.Linker().ReflectionSystemMetadata(represented);
    if (system.has_inner_class) return system.inner_name.value_or("");
    const auto name = ClassNameCodec::ClassGetName(context.vm.Linker().Class(represented).descriptor);
    const auto separator = name.rfind('.');
    return separator == std::string::npos ? name : name.substr(separator + 1);
}

[[nodiscard]] std::optional<std::string> CanonicalName(IntrinsicContext& context, const DexClassId represented) {
    if (const auto component = ComponentType(context, represented); component.has_value()) {
        const auto component_name = CanonicalName(context, *component);
        return component_name.has_value() ? std::optional<std::string>(*component_name + "[]") : std::nullopt;
    }
    const auto system = context.vm.Linker().ReflectionSystemMetadata(represented);
    if (system.enclosing_method.has_value() || (system.has_inner_class && !system.inner_name.has_value())) {
        return std::nullopt;
    }
    if (system.enclosing_class.has_value()) {
        const auto enclosing = CanonicalName(context, *system.enclosing_class);
        if (!enclosing.has_value() || !system.inner_name.has_value()) {
            return std::nullopt;
        }
        return *enclosing + "." + *system.inner_name;
    }
    return ClassNameCodec::ClassGetName(context.vm.Linker().Class(represented).descriptor);
}

[[nodiscard]] VmObjectRef EnclosingExecutable(IntrinsicContext& context, const bool constructor) {
    const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
    if (!system.enclosing_method.has_value()) return VmObjectRef{};
    const auto method = *system.enclosing_method;
    const auto& linked = context.vm.Linker().Method(method);
    if ((linked.name == "<init>") != constructor) return VmObjectRef{};
    if (constructor) {
        for (const auto& meta : context.vm.Reflection().DeclaredConstructors(linked.owner)) {
            if (meta.method == method) {
                return context.vm.Reflection().MaterializeConstructor(meta);
            }
        }
    } else {
        for (const auto& meta : context.vm.Reflection().DeclaredMethods(linked.owner)) {
            if (meta.method == method) {
                return context.vm.Reflection().MaterializeMethod(meta);
            }
        }
    }
    throw DexVmError(DexVmErrorReason::internal_invariant, "enclosing executable is not reflectable");
}

[[noreturn]] void ThrowNoSuchMethod(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchMethodException;", std::string(name)};
}

[[noreturn]] void ThrowNoSuchField(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchFieldException;", std::string(name)};
}

[[nodiscard]] VmValue SetClassNotFound(IntrinsicContext& context, const std::string_view name,
                                       const VmObjectRef cause = VmObjectRef{}) {
    const auto wrapper = context.vm.MakeThrowable("Ljava/lang/ClassNotFoundException;", name);
    if (cause.IsValid()) {
        const auto field = context.vm.Linker().FindFieldRecursive(context.vm.Model().ObjectClass(wrapper), "ex",
                                                                  "Ljava/lang/Throwable;");
        if (!field.has_value()) {
            throw DexVmError(DexVmErrorReason::internal_invariant, "ClassNotFoundException.ex is missing");
        }
        const auto& linked = context.vm.Linker().Field(*field);
        context.vm.Model().InstanceSlots(wrapper)[linked.slot] = {cause.Value(), SlotTag::ref};
    }
    context.vm.SetPendingException(wrapper);
    return VmValue::Ref(VmObjectRef{});
}

[[nodiscard]] VmValue ForName(IntrinsicContext& context, const std::string_view name, const bool initialize,
                              const VmClassLoaderId loader) {
    DexClassId java_class;
    try {
        java_class = context.vm.ClassLoaders().LoadClass(loader, name);
    } catch (const ClassNameCodecError&) {
        return SetClassNotFound(context, name);
    } catch (const JavaLinkageError& error) {
        const auto cause = context.vm.MakeThrowable(error.Descriptor(), error.what());
        return SetClassNotFound(context, name, cause);
    } catch (const DexVmError& error) {
        if (error.Reason() == DexVmErrorReason::unknown_class) return SetClassNotFound(context, name);
        throw;
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

IntrinsicClassDecl Declare_java_lang_Class(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Class;", "Ljava/lang/Object;",
                                                {"Ljava/io/Serializable;", "Ljava/lang/reflect/AnnotatedElement;",
                                                 "Ljava/lang/reflect/GenericDeclaration;", "Ljava/lang/reflect/Type;"},
                                                kAccPublic | kAccFinal);

    builder.StaticMethod("forName", "(Ljava/lang/String;)Ljava/lang/Class;", [](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto name = context.vm.StringUtf8(call.NonNullRef(0, "className"));
        // API19 Class.forName(String) obtains the raw defining loader of
        // the real caller. A bootstrap caller yields null, which the Java
        // three-argument overload normalizes to the system loader.
        auto role = kApplicationLoader;
        if (const auto caller = context.vm.CurrentCallerClass();
            caller.has_value() && context.vm.Linker().Class(*caller).defining_loader != kBootstrapLoader) {
            role = context.vm.Linker().Class(*caller).defining_loader;
        }
        return ForName(context, name, true, role);
    });
    builder.StaticMethod("forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",
                         [](IntrinsicContext& context) {
                             IntrinsicCall call(context);
                             const auto name = context.vm.StringUtf8(call.NonNullRef(0, "className"));
                             const auto loader_object = context.arguments[2].ref;
                             auto role = kApplicationLoader;
                             if (loader_object.IsValid()) {
                                 const auto resolved = context.vm.ClassLoaders().RoleOf(loader_object);
                                 if (!resolved.has_value()) {
                                     throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unknown class loader"};
                                 }
                                 role = *resolved;
                             }
                             return ForName(context, name, context.arguments[1].AsInt() != 0, role);
                         });

    builder.VirtualMethod("getName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.NewStringUtf8(
            ClassNameCodec::ClassGetName(context.vm.Linker().Class(Represented(context)).descriptor)));
    });
    // AOSP libdvm Class.java: strip one slash, or prepend the binary package.
    const auto resource = [services](IntrinsicContext& context, const bool stream) {
        auto name = context.vm.StringUtf8(IntrinsicCall(context).NonNullRef(0, "resourceName"));
        const auto& represented = context.vm.Linker().Class(Represented(context));
        if (name.starts_with('/')) {
            name.erase(name.begin());
        } else {
            const auto binary = ClassNameCodec::ClassGetName(represented.descriptor);
            const auto dot = binary.rfind('.');
            auto prefix = dot == std::string::npos ? std::string{} : binary.substr(0, dot);
            std::replace(prefix.begin(), prefix.end(), '.', '/');
            name = prefix + "/" + name;
        }
        // Primitive Class.getClassLoader() is null: API19 uses the system loader.
        const auto role = represented.descriptor.size() != 1 && represented.defining_loader == kBootstrapLoader
                              ? CoreIntrinsicServices::ClasspathLoader::bootstrap
                              : CoreIntrinsicServices::ClasspathLoader::application;
        return VmValue::Ref(stream ? detail::ClasspathResourceStream(context, services, role, name)
                                   : detail::ClasspathResourceUrl(context, services, role, name));
    };
    builder.VirtualMethod("getResource", "(Ljava/lang/String;)Ljava/net/URL;",
                          [resource](IntrinsicContext& context) { return resource(context, false); });
    builder.VirtualMethod("getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;",
                          [resource](IntrinsicContext& context) { return resource(context, true); });
    builder.VirtualMethod("getPackage", "()Ljava/lang/Package;", [](IntrinsicContext& context) {
        const auto descriptor = context.vm.Linker().Class(Represented(context)).descriptor;
        if (!descriptor.starts_with('L')) return VmValue::Ref(VmObjectRef{});
        const auto binary = ClassNameCodec::ClassGetName(descriptor);
        const auto dot = binary.rfind('.');
        if (dot == std::string::npos) return VmValue::Ref(VmObjectRef{});
        const auto type = context.vm.Linker().ResolveDescriptor("Ljava/lang/Package;");
        const auto package = context.vm.Model().NewInstance(type, context.vm.Linker().Class(type).instance_slots);
        const auto name = context.vm.NewStringUtf8(binary.substr(0, dot));
        const auto roots = context.vm.ProtectReferences(std::array{package, name});
        const auto constructor = context.vm.Linker().FindDirectMethod(
            type, "<init>",
            "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
            "Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/net/URL;)V");
        if (!constructor) throw DexVmError(DexVmErrorReason::unresolved_reference, "Package constructor");
        const auto null = VmValue::Ref(VmObjectRef{});
        const auto outcome = context.vm.Call(*constructor, std::array{VmValue::Ref(package), VmValue::Ref(name), null,
                                                                      null, null, null, null, null, null});
        if (outcome.exception.IsValid())
            throw VmJavaThrow{context.vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                              outcome.exception};
        return VmValue::Ref(package);
    });
    builder.VirtualMethod("desiredAssertionStatus", "()Z", [](IntrinsicContext& context) {
        // API 19 defaults assertions to disabled when no rule matches; only
        // the global -ea switch of standalone runs is modelled, not per-class rules.
        return VmValue::Int(context.vm.Config().enable_assertions ? 1 : 0);
    });
    builder.VirtualMethod("getSimpleName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.NewStringUtf8(SimpleName(context, Represented(context))));
    });
    builder.VirtualMethod("getCanonicalName", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto name = CanonicalName(context, Represented(context));
        return VmValue::Ref(name.has_value() ? context.vm.NewStringUtf8(*name) : VmObjectRef{});
    });
    builder.VirtualMethod("getDeclaringClass", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Ref(system.enclosing_class.has_value() && !system.enclosing_method.has_value()
                                ? context.vm.Model().ClassObject(*system.enclosing_class)
                                : VmObjectRef{});
    });
    builder.VirtualMethod("getEnclosingClass", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Ref(system.enclosing_class.has_value() ? context.vm.Model().ClassObject(*system.enclosing_class)
                                                               : VmObjectRef{});
    });
    builder.VirtualMethod("getEnclosingMethod", "()Ljava/lang/reflect/Method;",
                          [](IntrinsicContext& context) { return VmValue::Ref(EnclosingExecutable(context, false)); });
    builder.VirtualMethod("getEnclosingConstructor", "()Ljava/lang/reflect/Constructor;",
                          [](IntrinsicContext& context) { return VmValue::Ref(EnclosingExecutable(context, true)); });
    builder.VirtualMethod("isAnonymousClass", "()Z", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Int(system.has_inner_class && !system.inner_name.has_value() ? 1 : 0);
    });
    builder.VirtualMethod("isLocalClass", "()Z", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Int(system.enclosing_method.has_value() && system.inner_name.has_value() ? 1 : 0);
    });
    builder.VirtualMethod("isMemberClass", "()Z", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Int(system.enclosing_class.has_value() && !system.enclosing_method.has_value() ? 1 : 0);
    });
    builder.VirtualMethod("getDeclaredClasses", "()[Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto system = context.vm.Linker().ReflectionSystemMetadata(Represented(context));
        return VmValue::Ref(context.vm.Reflection().MaterializeTypeArray(system.member_classes));
    });
    builder.VirtualMethod("getClasses", "()[Ljava/lang/Class;", [](IntrinsicContext& c) {
        std::vector<DexClassId> members;
        auto type = std::optional{Represented(c)};
        while (type) {
            for (const auto member : c.vm.Linker().ReflectionSystemMetadata(*type).member_classes) {
                if ((c.vm.Linker().Class(member).access_flags & kAccPublic) != 0) members.push_back(member);
            }
            type = c.vm.Linker().Class(*type).super;
        }
        return VmValue::Ref(c.vm.Reflection().MaterializeTypeArray(members));
    });
    builder.VirtualMethod("getClassLoader", "()Ljava/lang/ClassLoader;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.ClassLoaders().LoaderForClass(Represented(context)));
    });
    builder.VirtualMethod("getComponentType", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto component = ComponentType(context, Represented(context));
        return VmValue::Ref(component.has_value() ? context.vm.Model().ClassObject(*component) : VmObjectRef{});
    });
    builder.VirtualMethod("getSuperclass", "()Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto represented = Represented(context);
        const auto& linked = context.vm.Linker().Class(represented);
        if (IsPrimitiveDescriptor(linked.descriptor) || linked.is_interface || !linked.super.has_value()) {
            return VmValue::Ref(VmObjectRef{});
        }
        return VmValue::Ref(context.vm.Model().ClassObject(*linked.super));
    });
    builder.VirtualMethod("getInterfaces", "()[Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto interfaces = DirectInterfaces(context, Represented(context));
        return VmValue::Ref(context.vm.Reflection().MaterializeTypeArray(interfaces));
    });
    builder.VirtualMethod("getModifiers", "()I", [](IntrinsicContext& context) {
        return VmValue::Int(static_cast<std::int32_t>(ClassModifiers(context, Represented(context))));
    });
    builder.VirtualMethod("isArray", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Linker().Class(Represented(context)).is_array ? 1 : 0);
    });
    builder.VirtualMethod("isInterface", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(context.vm.Linker().Class(Represented(context)).is_interface ? 1 : 0);
    });
    builder.VirtualMethod("isPrimitive", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(IsPrimitiveDescriptor(context.vm.Linker().Class(Represented(context)).descriptor) ? 1 : 0);
    });
    builder.VirtualMethod("isAnnotation", "()Z", [](IntrinsicContext& c) {
        return VmValue::Int((c.vm.Linker().Class(Represented(c)).access_flags & kAccAnnotation) != 0);
    });
    builder.VirtualMethod("isEnum", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(IsEnumClass(context, Represented(context)) ? 1 : 0);
    });
    builder.VirtualMethod("getEnumConstants", "()[Ljava/lang/Object;", [](IntrinsicContext& context) {
        const auto represented = Represented(context);
        if (!IsEnumClass(context, represented)) {
            return VmValue::Ref(VmObjectRef{});
        }
        const auto shared = context.vm.SharedEnumConstants(represented);
        const auto roots = context.vm.ProtectReferences(std::array{shared});
        const auto clone = context.vm.Model().CloneObject(shared);
        static_cast<void>(roots);
        return VmValue::Ref(clone);
    });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int((ClassModifiers(context, Represented(context)) & kAccSynthetic) != 0U ? 1 : 0);
    });
    builder.VirtualMethod("isInstance", "(Ljava/lang/Object;)Z", [](IntrinsicContext& context) {
        const auto object = context.arguments[0].ref;
        return VmValue::Int(object.IsValid() && context.vm.Linker().IsAssignable(Represented(context),
                                                                                 context.vm.Model().ObjectClass(object))
                                ? 1
                                : 0);
    });
    builder.VirtualMethod("isAssignableFrom", "(Ljava/lang/Class;)Z", [](IntrinsicContext& context) {
        const auto candidate = context.arguments[0].ref;
        if (!candidate.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;", "cls == null"};
        }
        return VmValue::Int(
            context.vm.Linker().IsAssignable(Represented(context), context.vm.Model().ClassOfClassObject(candidate))
                ? 1
                : 0);
    });
    builder.VirtualMethod("cast", "(Ljava/lang/Object;)Ljava/lang/Object;", [](IntrinsicContext& context) {
        const auto object = context.arguments[0].ref;
        if (!object.IsValid()) return VmValue::Ref(VmObjectRef{});
        const auto desired = Represented(context);
        const auto actual = context.vm.Model().ObjectClass(object);
        if (context.vm.Linker().IsAssignable(desired, actual)) {
            return VmValue::Ref(object);
        }
        throw VmJavaThrow{"Ljava/lang/ClassCastException;",
                          ClassNameCodec::ClassGetName(context.vm.Linker().Class(actual).descriptor) +
                              " cannot be cast to " +
                              ClassNameCodec::ClassGetName(context.vm.Linker().Class(desired).descriptor)};
    });
    builder.VirtualMethod("asSubclass", "(Ljava/lang/Class;)Ljava/lang/Class;", [](IntrinsicContext& context) {
        const auto desired_object = context.arguments[0].ref;
        if (!desired_object.IsValid()) {
            throw VmJavaThrow{"Ljava/lang/NullPointerException;", "clazz == null"};
        }
        const auto represented = Represented(context);
        const auto desired = context.vm.Model().ClassOfClassObject(desired_object);
        if (context.vm.Linker().IsAssignable(desired, represented)) {
            return VmValue::Ref(context.receiver);
        }
        throw VmJavaThrow{"Ljava/lang/ClassCastException;",
                          ClassNameCodec::ClassGetName(context.vm.Linker().Class(represented).descriptor) +
                              " cannot be cast to " +
                              ClassNameCodec::ClassGetName(context.vm.Linker().Class(desired).descriptor)};
    });
    builder.OverrideMethod("toString", "()Ljava/lang/String;", [](IntrinsicContext& context) {
        const auto represented = Represented(context);
        const auto& linked = context.vm.Linker().Class(represented);
        const auto text = IsPrimitiveDescriptor(linked.descriptor)
                              ? SimpleName(context, represented)
                              : std::string(linked.is_interface ? "interface " : "class ") +
                                    ClassNameCodec::ClassGetName(linked.descriptor);
        return VmValue::Ref(context.vm.NewStringUtf8(text));
    });
    builder.VirtualMethod("newInstance", "()Ljava/lang/Object;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.Reflection().NewInstance(Represented(context), context.vm.CurrentCallerClass()));
    });

    // Dalvik API19 java_lang_Class.cpp / dvmGetDeclaredConstructorOrMethod.
    builder.StaticMethod(
        "getDeclaredConstructorOrMethod",
        "(Ljava/lang/Class;Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Member;",
        [](IntrinsicContext& context) {
            const auto type = context.vm.Model().ClassOfClassObject(IntrinsicCall(context).NonNullRef(0, "clazz"));
            const auto name = RequireName(context, context.arguments[1].ref);
            const auto array = context.arguments[2].ref;
            // A null parameter entry cannot match a declared parameter; this native
            // returns null rather than getDeclaredMethod's NoSuchMethodException.
            if (array.IsValid()) {
                for (JniSize i = 0; i < context.vm.Model().ArrayLength(array); ++i)
                    if (!context.vm.Model().GetObjectElement(array, i).IsValid()) return VmValue::Ref(VmObjectRef{});
            }
            const auto parameters = ParameterTypes(context, array);
            if (name == "<init>") {
                const auto member = context.vm.Reflection().FindConstructor(type, parameters, false);
                return VmValue::Ref(member ? context.vm.Reflection().MaterializeConstructor(*member) : VmObjectRef{});
            }
            const auto member = context.vm.Reflection().FindDeclaredMethod(type, name, parameters);
            return VmValue::Ref(member ? context.vm.Reflection().MaterializeMethod(*member) : VmObjectRef{});
        },
        kAccStatic | kAccNative);

    builder.VirtualMethod("getDeclaredMethods", "()[Ljava/lang/reflect/Method;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.Reflection().MaterializeDeclaredMethods(Represented(context)));
    });
    builder.VirtualMethod("getMethods", "()[Ljava/lang/reflect/Method;", [](IntrinsicContext& context) {
        const auto methods = context.vm.Reflection().PublicMethods(Represented(context));
        return VmValue::Ref(context.vm.Reflection().MaterializeMethods(methods));
    });
    builder.VirtualMethod("getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                          [](IntrinsicContext& context) {
                              const auto name = RequireName(context, context.arguments[0].ref);
                              const auto parameters = ParameterTypes(context, context.arguments[1].ref);
                              const auto method =
                                  context.vm.Reflection().FindDeclaredMethod(Represented(context), name, parameters);
                              if (!method.has_value()) ThrowNoSuchMethod(name);
                              return VmValue::Ref(context.vm.Reflection().MaterializeMethod(*method));
                          });
    builder.VirtualMethod(
        "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;", [](IntrinsicContext& context) {
            const auto name = RequireName(context, context.arguments[0].ref);
            const auto parameters = ParameterTypes(context, context.arguments[1].ref);
            const auto method = context.vm.Reflection().FindPublicMethod(Represented(context), name, parameters);
            if (!method.has_value()) ThrowNoSuchMethod(name);
            return VmValue::Ref(context.vm.Reflection().MaterializeMethod(*method));
        });

    builder.VirtualMethod("getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;",
                          [](IntrinsicContext& context) {
                              return VmValue::Ref(context.vm.Reflection().MaterializeConstructors(
                                  context.vm.Reflection().DeclaredConstructors(Represented(context))));
                          });
    builder.VirtualMethod("getConstructors", "()[Ljava/lang/reflect/Constructor;", [](IntrinsicContext& context) {
        std::vector<ReflectConstructorMeta> constructors;
        for (const auto& constructor : context.vm.Reflection().DeclaredConstructors(Represented(context))) {
            if ((constructor.access_flags & kAccPublic) != 0U) {
                constructors.push_back(constructor);
            }
        }
        return VmValue::Ref(context.vm.Reflection().MaterializeConstructors(constructors));
    });
    const auto add_constructor_lookup = [&](const std::string& name, const bool public_only) {
        builder.VirtualMethod(
            name, "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;", [public_only](IntrinsicContext& context) {
                const auto parameters = ParameterTypes(context, context.arguments[0].ref);
                const auto constructor =
                    context.vm.Reflection().FindConstructor(Represented(context), parameters, public_only);
                if (!constructor.has_value()) ThrowNoSuchMethod("<init>");
                return VmValue::Ref(context.vm.Reflection().MaterializeConstructor(*constructor));
            });
    };
    add_constructor_lookup("getDeclaredConstructor", false);
    add_constructor_lookup("getConstructor", true);

    builder.VirtualMethod("getDeclaredFields", "()[Ljava/lang/reflect/Field;", [](IntrinsicContext& context) {
        return VmValue::Ref(
            context.vm.Reflection().MaterializeFields(context.vm.Reflection().DeclaredFields(Represented(context))));
    });
    builder.VirtualMethod("getFields", "()[Ljava/lang/reflect/Field;", [](IntrinsicContext& context) {
        const auto fields = context.vm.Reflection().PublicFields(Represented(context));
        return VmValue::Ref(context.vm.Reflection().MaterializeFields(fields));
    });
    const auto add_field_lookup = [&](const std::string& method_name, const bool public_only) {
        builder.VirtualMethod(
            method_name, "(Ljava/lang/String;)Ljava/lang/reflect/Field;", [public_only](IntrinsicContext& context) {
                const auto name = RequireName(context, context.arguments[0].ref);
                const auto field = public_only ? context.vm.Reflection().FindPublicField(Represented(context), name)
                                               : context.vm.Reflection().FindDeclaredField(Represented(context), name);
                if (!field.has_value()) ThrowNoSuchField(name);
                return VmValue::Ref(context.vm.Reflection().MaterializeField(*field));
            });
    };
    add_field_lookup("getDeclaredField", false);
    add_field_lookup("getField", true);

    builder.VirtualMethod("isAnnotationPresent", "(Ljava/lang/Class;)Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            context.vm.Annotations().IsClassAnnotationPresent(Represented(context), context.arguments[0].ref) ? 1 : 0);
    });
    builder.VirtualMethod(
        "getAnnotation", "(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;", [](IntrinsicContext& context) {
            return VmValue::Ref(
                context.vm.Annotations().GetClassAnnotation(Represented(context), context.arguments[0].ref, true));
        });
    builder.VirtualMethod("getAnnotations", "()[Ljava/lang/annotation/Annotation;", [](IntrinsicContext& context) {
        return VmValue::Ref(context.vm.Annotations().GetClassAnnotations(Represented(context), true));
    });
    builder.VirtualMethod(
        "getDeclaredAnnotations", "()[Ljava/lang/annotation/Annotation;", [](IntrinsicContext& context) {
            return VmValue::Ref(context.vm.Annotations().GetClassAnnotations(Represented(context), false));
        });

    builder.VirtualMethod("getGenericSuperclass", "()Ljava/lang/reflect/Type;", [](IntrinsicContext& c) {
        return VmValue::Ref(ReflectGenericType(c.vm, c.receiver, "getGenericSuperclass"));
    });
    builder.VirtualMethod("getGenericInterfaces", "()[Ljava/lang/reflect/Type;", [](IntrinsicContext& c) {
        return VmValue::Ref(ReflectGenericType(c.vm, c.receiver, "getGenericInterfaces"));
    });
    builder.VirtualMethod("getTypeParameters", "()[Ljava/lang/reflect/TypeVariable;", [](IntrinsicContext& c) {
        return VmValue::Ref(ReflectGenericType(c.vm, c.receiver, "getTypeParameters"));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_reflect_Type.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_libcore_reflect_AnnotationAccess() {
    auto b = IntrinsicClassBuilder::Class("Llibcore/reflect/AnnotationAccess;");
    b.StaticMethod("getEnclosingMethodOrConstructor", "(Ljava/lang/Class;)Ljava/lang/reflect/AccessibleObject;",
                   [](IntrinsicContext& c) {
                       const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
                       const auto metadata = c.vm.Linker().ReflectionSystemMetadata(type);
                       if (!metadata.enclosing_method) return VmValue::Ref(VmObjectRef{});
                       const auto method = *metadata.enclosing_method;
                       const auto& linked = c.vm.Linker().Method(method);
                       if (linked.name == "<init>") {
                           for (const auto& candidate : c.vm.Reflection().DeclaredConstructors(linked.owner))
                               if (candidate.method == method)
                                   return VmValue::Ref(c.vm.Reflection().MaterializeConstructor(candidate));
                       } else {
                           const auto candidate = c.vm.Reflection().FindDeclaredMethodByDescriptor(
                               linked.owner, linked.name, linked.descriptor);
                           if (candidate) return VmValue::Ref(c.vm.Reflection().MaterializeMethod(*candidate));
                       }
                       throw DexVmError(DexVmErrorReason::unresolved_reference, "enclosing generic declaration");
                   });
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Proxy() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Proxy;");
    b.MergesBootClass();
    b.StaticMethod(
        "generateProxy", "(Ljava/lang/String;[Ljava/lang/Class;Ljava/lang/ClassLoader;)Ljava/lang/Class;",
        [](IntrinsicContext& context) {
            const auto name = IntrinsicCall(context).NonNullRef(0, "name");
            const auto type = GenerateProxyClass(context, context.vm.StringUtf8(name), context.arguments[1].ref,
                                                 context.arguments[2].ref);
            return VmValue::Ref(context.vm.Model().ClassObject(type));
        },
        kAccPrivate | kAccNative);
    b.UnimplementedStatic("constructorPrototype", "(Ljava/lang/reflect/InvocationHandler;)V", kAccPrivate | kAccNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
// Native handlers have no Java frame. Skip the ROM invoke/get/set/newInstance frame,
// matching Dalvik dvmGetCaller2Class (native + Java reflection entry).
std::optional<DexClassId> ReflectionCaller(Interpreter& vm) {
    const auto stack = vm.CallingStackClasses(1, 1);
    return stack.empty() ? std::nullopt : std::optional<DexClassId>(stack.front());
}

DexClassId ReflectionClass(IntrinsicContext& c, std::size_t index) {
    return c.vm.Model().ClassOfClassObject(c.arguments[index].ref);
}

VmValue SignatureFragments(Interpreter& vm, const std::optional<std::u16string>& signature) {
    if (!signature) return VmValue::Ref(VmObjectRef{});
    const auto array_type = vm.Linker().ResolveDescriptor("[Ljava/lang/Object;");
    const auto object_type = vm.Linker().ResolveDescriptor("Ljava/lang/Object;");
    const auto array = vm.Model().NewObjectArray(array_type, object_type, 1);
    const auto roots = vm.ProtectReferences(std::array{array});
    vm.Model().SetObjectElement(array, 0, vm.Model().NewString(*signature));
    return VmValue::Ref(array);
}

VmObjectRef NewReflectionArray(IntrinsicContext& c, const std::vector<std::int32_t>& dimensions) {
    for (const auto length : dimensions)
        if (length < 0) throw VmJavaThrow{"Ljava/lang/NegativeArraySizeException;", std::to_string(length)};
    return detail::BuildReflectArray(c.vm, c.vm.Linker().Class(ReflectionClass(c, 0)).descriptor, dimensions);
}
}  // namespace

IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/AccessibleObject;");
    b.MergesBootClass();
    b.StaticMethod(
        "getClassSignatureAnnotation", "(Ljava/lang/Class;)[Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            return SignatureFragments(c.vm, c.vm.Linker().Class(ReflectionClass(c, 0)).generic_signature);
        },
        kAccPrivate | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Array() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Array;");
    b.MergesBootClass();
    b.StaticMethod(
        "createMultiArray", "(Ljava/lang/Class;[I)Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto dimensions = c.arguments[1].ref;
            std::vector<std::int32_t> lengths;
            for (JniSize i = 0; i < c.vm.Model().ArrayLength(dimensions); ++i)
                lengths.push_back(static_cast<std::int32_t>(c.vm.Model().GetPrimitiveElement(dimensions, i)));
            return VmValue::Ref(NewReflectionArray(c, lengths));
        },
        kAccPrivate | kAccNative);
    b.StaticMethod(
        "createObjectArray", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        [](IntrinsicContext& c) { return VmValue::Ref(NewReflectionArray(c, {c.arguments[1].AsInt()})); },
        kAccPrivate | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Constructor() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Constructor;");
    b.MergesBootClass();
    b.DirectMethod(
        "constructNative", "([Ljava/lang/Object;Ljava/lang/Class;[Ljava/lang/Class;IZ)Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().ConstructorMetadata(
                ReflectionClass(c, 1), static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            return VmValue::Ref(c.vm.Reflection().InvokeConstructor(meta, c.arguments[0].ref, ReflectionCaller(c.vm),
                                                                    c.arguments[4].AsInt() != 0));
        },
        kAccPrivate | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Field() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Field;");
    b.MergesBootClass();
    b.StaticMethod(
        "getAnnotation", "(Ljava/lang/Class;ILjava/lang/Class;)Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetFieldAnnotation(meta.field, c.arguments[2].ref));
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getBField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)B",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getCField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)C",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getDField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)D",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.StaticMethod(
        "getDeclaredAnnotations", "(Ljava/lang/Class;I)[Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetFieldAnnotations(meta.field));
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getFField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)F",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZ)Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::nullopt, ReflectionCaller(c.vm),
                                              c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getFieldModifiers", "(Ljava/lang/Class;I)I",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Int(static_cast<std::int32_t>(meta.access_flags & 0xffffU));
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getIField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)I",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getJField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)J",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getSField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)S",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getSignatureAnnotation", "(Ljava/lang/Class;I)[Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return SignatureFragments(c.vm, c.vm.Linker().Field(meta.field).generic_signature);
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "getZField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC)Z",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            return c.vm.Reflection().GetField(meta, c.arguments[0].ref, std::optional<DexClassId>(primitive),
                                              ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
        },
        kAccPrivate | kAccNative);
    b.StaticMethod(
        "isAnnotationPresent", "(Ljava/lang/Class;ILjava/lang/Class;)Z",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Int(c.vm.Annotations().IsFieldAnnotationPresent(meta.field, c.arguments[2].ref));
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setBField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCB)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setCField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCC)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setDField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCD)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setFField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCF)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZLjava/lang/Object;)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[5], std::nullopt, ReflectionCaller(c.vm),
                                       c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setIField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCI)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setJField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCJ)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setSField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCS)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    b.DirectMethod(
        "setZField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZCZ)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
            const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
            if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], std::optional<DexClassId>(primitive),
                                       ReflectionCaller(c.vm), c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Method() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Method;");
    b.MergesBootClass();
    b.StaticMethod(
        "getAnnotation", "(Ljava/lang/Class;ILjava/lang/Class;)Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetMethodAnnotation(meta.method, c.arguments[2].ref));
        },
        kAccNative);
    b.StaticMethod(
        "getDeclaredAnnotations", "(Ljava/lang/Class;I)[Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetMethodAnnotations(meta.method));
        },
        kAccNative);
    b.DirectMethod(
        "getDefaultValue", "(Ljava/lang/Class;I)Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetDefaultValue(meta.method));
        },
        kAccPrivate | kAccNative);
    b.StaticMethod(
        "getMethodModifiers", "(Ljava/lang/Class;I)I",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            auto flags = meta.access_flags;
            if ((flags & kAccAbstract) != 0U) flags &= ~kAccNative;
            flags &= ~kAccSynchronized;
            if ((flags & kAccDeclaredSynchronized) != 0U) flags |= kAccSynchronized;
            return VmValue::Int(static_cast<std::int32_t>(flags & 0xffffU));
        },
        kAccNative);
    b.StaticMethod(
        "getParameterAnnotations", "(Ljava/lang/Class;I)[[Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetParameterAnnotations(meta.method));
        },
        kAccNative);
    b.StaticMethod(
        "getSignatureAnnotation", "(Ljava/lang/Class;I)[Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return SignatureFragments(c.vm, c.vm.Linker().Method(meta.method).generic_signature);
        },
        kAccNative);
    b.DirectMethod(
        "invokeNative",
        "(Ljava/lang/Object;[Ljava/lang/Object;Ljava/lang/Class;[Ljava/lang/Class;Ljava/lang/Class;IZ)Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 2),
                                                                static_cast<std::uint32_t>(c.arguments[5].AsInt()));
            return VmValue::Ref(c.vm.Reflection().InvokeMethod(meta, c.arguments[0].ref, c.arguments[1].ref,
                                                               ReflectionCaller(c.vm), c.arguments[6].AsInt() != 0));
        },
        kAccPrivate | kAccNative);
    b.StaticMethod(
        "isAnnotationPresent", "(Ljava/lang/Class;ILjava/lang/Class;)Z",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().MethodMetadata(ReflectionClass(c, 0),
                                                                static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Int(c.vm.Annotations().GetMethodAnnotation(meta.method, c.arguments[2].ref).IsValid());
        },
        kAccNative);
    return std::move(b).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics
