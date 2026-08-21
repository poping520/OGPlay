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

constexpr std::uint32_t kJavaFlagsMask = 0xffffU;
constexpr std::uint32_t kAccInterface = 0x0200U;
constexpr std::uint32_t kAccFinal = 0x0010U;
constexpr std::uint32_t kAccAbstract = 0x0400U;
constexpr std::uint32_t kAccSynthetic = 0x1000U;
constexpr std::uint32_t kAccEnum = 0x4000U;

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
        return 0x0001U | kAccFinal | kAccAbstract;
    }
    if (!linked.is_array) return linked.access_flags & kJavaFlagsMask;

    std::string_view leaf = linked.descriptor;
    while (leaf.starts_with("[")) leaf.remove_prefix(1);
    const auto leaf_class = linker.ResolveDescriptor(leaf);
    return ((ClassModifiers(context, leaf_class) & kJavaFlagsMask) &
            ~kAccInterface) |
           kAccFinal | kAccAbstract;
}

[[nodiscard]] std::string SimpleName(IntrinsicContext& context,
                                     const DexClassId represented) {
    if (const auto component = ComponentType(context, represented);
        component.has_value()) {
        return SimpleName(context, *component) + "[]";
    }
    const auto name = ClassNameCodec::ClassGetName(
        context.vm.Linker().Class(represented).descriptor);
    const auto separator = name.rfind('.');
    return separator == std::string::npos ? name : name.substr(separator + 1);
}

[[noreturn]] void ThrowNoSuchMethod(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchMethodException;", std::string(name)};
}

[[noreturn]] void ThrowNoSuchField(const std::string_view name) {
    throw VmJavaThrow{"Ljava/lang/NoSuchFieldException;", std::string(name)};
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_Class() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/Class;", "Ljava/lang/Object;",
        {"Ljava/io/Serializable;", "Ljava/lang/reflect/AnnotatedElement;",
         "Ljava/lang/reflect/GenericDeclaration;",
         "Ljava/lang/reflect/Type;"},
        0x0011U);

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
    builder.VirtualMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto represented = Represented(context);
            const auto& linked = context.vm.Linker().Class(represented);
            const auto text = IsPrimitiveDescriptor(linked.descriptor)
                ? SimpleName(context, represented)
                : std::string(linked.is_interface ? "interface " : "class ") +
                      ClassNameCodec::ClassGetName(linked.descriptor);
            return VmValue::Ref(context.vm.NewStringUtf8(text));
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
                if ((constructor.access_flags & 0x0001U) != 0U) {
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
