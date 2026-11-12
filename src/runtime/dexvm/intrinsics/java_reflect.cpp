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

[[nodiscard]] std::uint32_t ClassModifiers(IntrinsicContext& context, const DexClassId represented,
                                           const bool ignore_inner = false) {
    auto& linker = context.vm.Linker();
    const auto& linked = linker.Class(represented);
    if (IsPrimitiveDescriptor(linked.descriptor)) {
        return kAccPublic | kAccFinal | kAccAbstract;
    }
    if (!linked.is_array) {
        const auto system = linker.ReflectionSystemMetadata(represented);
        return (!ignore_inner && system.has_inner_class ? system.inner_access_flags : linked.access_flags) &
               kAccJavaFlagsMask;
    }

    std::string_view leaf = linked.descriptor;
    while (leaf.starts_with("["))
        leaf.remove_prefix(1);
    const auto leaf_class = linker.ResolveDescriptor(leaf);
    return ((ClassModifiers(context, leaf_class, ignore_inner) & kAccJavaFlagsMask) & ~kAccInterface) | kAccFinal |
           kAccAbstract;
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

IntrinsicClassDecl Declare_java_lang_Class_Natives(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/Class;");
    builder.MergesBootClass();
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

    builder.StaticMethod(
        "classForName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto name = RequireName(c, c.arguments[0].ref);
            const auto loader = c.arguments[2].ref;
            const auto role = loader.IsValid() ? c.vm.ClassLoaders().RoleOf(loader)
                                               : std::optional<VmClassLoaderId>(kBootstrapLoader);
            if (!role) throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unknown class loader"};
            return ForName(c, name, c.arguments[1].AsInt() != 0, *role);
        },
        kAccPrivate | kAccStatic | kAccNative);
    builder.StaticMethod(
        "getClassLoader", "(Ljava/lang/Class;)Ljava/lang/ClassLoader;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            return VmValue::Ref(c.vm.Linker().ClassHeader(type).defining_loader == kBootstrapLoader
                                    ? VmObjectRef{}
                                    : c.vm.ClassLoaders().LoaderForClass(type));
        },
        kAccPrivate | kAccStatic | kAccNative);
    builder.DirectMethod(
        "getDeclaredAnnotation", "(Ljava/lang/Class;)Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            return VmValue::Ref(c.vm.Annotations().GetClassAnnotation(Represented(c), c.arguments[0].ref, false));
        },
        kAccPrivate | kAccNative);
    builder.StaticMethod(
        "getDeclaredClasses", "(Ljava/lang/Class;Z)[Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            std::vector<DexClassId> selected;
            for (const auto member : c.vm.Linker().ReflectionSystemMetadata(type).member_classes)
                if (!c.arguments[1].AsInt() || (c.vm.Linker().ClassHeader(member).access_flags & kAccPublic) != 0U)
                    selected.push_back(member);
            return VmValue::Ref(c.vm.Reflection().MaterializeTypeArray(selected));
        },
        kAccPrivate | kAccStatic | kAccNative);
    builder.StaticMethod(
        "getDeclaredConstructors", "(Ljava/lang/Class;Z)[Ljava/lang/reflect/Constructor;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            std::vector<ReflectConstructorMeta> selected;
            for (const auto& item : c.vm.Reflection().DeclaredConstructors(type))
                if (!c.arguments[1].AsInt() || (item.access_flags & kAccPublic) != 0U) selected.push_back(item);
            return VmValue::Ref(c.vm.Reflection().MaterializeConstructors(selected));
        },
        kAccPrivate | kAccStatic | kAccNative);
    builder.StaticMethod(
        "getDeclaredField", "(Ljava/lang/Class;Ljava/lang/String;)Ljava/lang/reflect/Field;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            const auto field = c.vm.Reflection().FindDeclaredField(type, RequireName(c, c.arguments[1].ref));
            return VmValue::Ref(field ? c.vm.Reflection().MaterializeField(*field) : VmObjectRef{});
        },
        kAccStatic | kAccNative);
    builder.StaticMethod(
        "getDeclaredFields", "(Ljava/lang/Class;Z)[Ljava/lang/reflect/Field;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            std::vector<ReflectFieldMeta> selected;
            for (const auto& item : c.vm.Reflection().DeclaredFields(type))
                if (!c.arguments[1].AsInt() || (item.access_flags & kAccPublic) != 0U) selected.push_back(item);
            return VmValue::Ref(c.vm.Reflection().MaterializeFields(selected));
        },
        kAccStatic | kAccNative);
    builder.StaticMethod(
        "getDeclaredMethods", "(Ljava/lang/Class;Z)[Ljava/lang/reflect/Method;",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            std::vector<ReflectMethodMeta> selected;
            for (const auto& item : c.vm.Reflection().DeclaredMethods(type))
                if (!c.arguments[1].AsInt() || (item.access_flags & kAccPublic) != 0U) selected.push_back(item);
            return VmValue::Ref(c.vm.Reflection().MaterializeMethods(selected));
        },
        kAccStatic | kAccNative);
    builder.DirectMethod(
        "getInnerClassName", "()Ljava/lang/String;",
        [](IntrinsicContext& c) {
            const auto system = c.vm.Linker().ReflectionSystemMetadata(Represented(c));
            return VmValue::Ref(system.has_inner_class && system.inner_name ? c.vm.NewStringUtf8(*system.inner_name)
                                                                            : VmObjectRef{});
        },
        kAccPrivate | kAccNative);
    builder.StaticMethod(
        "getModifiers", "(Ljava/lang/Class;Z)I",
        [](IntrinsicContext& c) {
            const auto type = c.vm.Model().ClassOfClassObject(c.arguments[0].ref);
            return VmValue::Int(static_cast<std::int32_t>(ClassModifiers(c, type, c.arguments[1].AsInt() != 0)));
        },
        kAccPrivate | kAccStatic | kAccNative);
    builder.DirectMethod(
        "getNameNative", "()Ljava/lang/String;",
        [](IntrinsicContext& c) {
            return VmValue::Ref(
                c.vm.NewStringUtf8(ClassNameCodec::ClassGetName(c.vm.Linker().ClassHeader(Represented(c)).descriptor)));
        },
        kAccPrivate | kAccNative);
    builder.DirectMethod(
        "isDeclaredAnnotationPresent", "(Ljava/lang/Class;)Z",
        [](IntrinsicContext& c) {
            return VmValue::Int(
                c.vm.Annotations().IsDeclaredClassAnnotationPresent(Represented(c), c.arguments[0].ref));
        },
        kAccPrivate | kAccNative);
    builder.DirectMethod(
        "newInstanceImpl", "()Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            // Dalvik dvmGetCaller2Class: native has no Java frame here; skip Class.newInstance.
            const auto caller = c.vm.CallingStackClasses(1, 1);
            return VmValue::Ref(c.vm.Reflection().NewInstance(
                Represented(c), caller.empty() ? std::nullopt : std::optional<DexClassId>(caller.front())));
        },
        kAccPrivate | kAccNative);
    builder.VirtualMethod(
        "desiredAssertionStatus", "()Z",
        [](IntrinsicContext& c) { return VmValue::Int(c.vm.Config().enable_assertions); }, kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getComponentType", "()Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto type = ComponentType(c, Represented(c));
            return VmValue::Ref(type ? c.vm.Model().ClassObject(*type) : VmObjectRef{});
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getDeclaredAnnotations", "()[Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) { return VmValue::Ref(c.vm.Annotations().GetClassAnnotations(Represented(c), false)); },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getDeclaringClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto system = c.vm.Linker().ReflectionSystemMetadata(Represented(c));
            return VmValue::Ref(system.enclosing_class && !system.enclosing_method
                                    ? c.vm.Model().ClassObject(*system.enclosing_class)
                                    : VmObjectRef{});
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getDex", "()Lcom/android/dex/Dex;",
        [services](IntrinsicContext& c) {
            return VmValue::Ref(c.vm.Reflection().DexObject(Represented(c), services.create_dex_buffer));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getEnclosingClass", "()Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto system = c.vm.Linker().ReflectionSystemMetadata(Represented(c));
            return VmValue::Ref(system.enclosing_class ? c.vm.Model().ClassObject(*system.enclosing_class)
                                                       : VmObjectRef{});
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getEnclosingConstructor", "()Ljava/lang/reflect/Constructor;",
        [](IntrinsicContext& c) { return VmValue::Ref(EnclosingExecutable(c, true)); }, kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getEnclosingMethod", "()Ljava/lang/reflect/Method;",
        [](IntrinsicContext& c) { return VmValue::Ref(EnclosingExecutable(c, false)); }, kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getInterfaces", "()[Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            return VmValue::Ref(c.vm.Reflection().MaterializeTypeArray(DirectInterfaces(c, Represented(c))));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "getSuperclass", "()Ljava/lang/Class;",
        [](IntrinsicContext& c) {
            const auto& type = c.vm.Linker().Class(Represented(c));
            return VmValue::Ref(!IsPrimitiveDescriptor(type.descriptor) && !type.is_interface && type.super
                                    ? c.vm.Model().ClassObject(*type.super)
                                    : VmObjectRef{});
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "isAnonymousClass", "()Z",
        [](IntrinsicContext& c) {
            const auto system = c.vm.Linker().ReflectionSystemMetadata(Represented(c));
            return VmValue::Int(system.has_inner_class && !system.inner_name);
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "isAssignableFrom", "(Ljava/lang/Class;)Z",
        [](IntrinsicContext& c) {
            const auto type = IntrinsicCall(c).NonNullRef(0, "cls");
            return VmValue::Int(c.vm.Linker().IsAssignable(Represented(c), c.vm.Model().ClassOfClassObject(type)));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "isInstance", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& c) {
            const auto object = c.arguments[0].ref;
            return VmValue::Int(object.IsValid() &&
                                c.vm.Linker().IsAssignable(Represented(c), c.vm.Model().ObjectClass(object)));
        },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "isInterface", "()Z",
        [](IntrinsicContext& c) { return VmValue::Int(c.vm.Linker().ClassHeader(Represented(c)).is_interface); },
        kAccPublic | kAccNative);
    builder.VirtualMethod(
        "isPrimitive", "()Z",
        [](IntrinsicContext& c) {
            return VmValue::Int(IsPrimitiveDescriptor(c.vm.Linker().ClassHeader(Represented(c)).descriptor));
        },
        kAccPublic | kAccNative);
    return std::move(builder).Build();
}
}  // namespace ogplay::runtime::dexvm::intrinsics

// ---- migrated from java_lang_reflect_Type.cpp ----
#include "catalog.h"

#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"

namespace ogplay::runtime::dexvm::intrinsics {

IntrinsicClassDecl Declare_java_lang_reflect_Proxy_Natives() {
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

IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject_Natives() {
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

IntrinsicClassDecl Declare_java_lang_reflect_Array_Natives() {
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

IntrinsicClassDecl Declare_java_lang_reflect_Constructor_Natives() {
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

IntrinsicClassDecl Declare_java_lang_reflect_Field_Natives() {
    auto b = IntrinsicClassBuilder::Class("Ljava/lang/reflect/Field;");
    b.MergesBootClass();
    for (const char type : std::string_view("ZBCSIJFD")) {
        const std::string suffix(1, type);
        const std::string prefix = "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZC";
        b.DirectMethod(
            "get" + suffix + "Field", prefix + ")" + suffix,
            [](IntrinsicContext& c) {
                const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                                   static_cast<std::uint32_t>(c.arguments[3].AsInt()));
                const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
                const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
                if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
                return c.vm.Reflection().GetField(meta, c.arguments[0].ref, primitive, ReflectionCaller(c.vm),
                                                  c.arguments[4].AsInt() != 0);
            },
            kAccPrivate | kAccNative);
        b.DirectMethod(
            "set" + suffix + "Field", prefix + suffix + ")V",
            [](IntrinsicContext& c) {
                const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                                   static_cast<std::uint32_t>(c.arguments[3].AsInt()));
                const std::string descriptor(1, static_cast<char>(c.arguments[5].AsInt()));
                const auto primitive = c.vm.Linker().ResolveDescriptor(descriptor);
                if (c.vm.Linker().Class(meta.type).descriptor.size() != 1U)
                    throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "not a primitive field"};
                c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[6], primitive, ReflectionCaller(c.vm),
                                           c.arguments[4].AsInt() != 0);
                return VmValue::Void();
            },
            kAccPrivate | kAccNative);
    }

    b.StaticMethod(
        "getAnnotation", "(Ljava/lang/Class;ILjava/lang/Class;)Ljava/lang/annotation/Annotation;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return VmValue::Ref(c.vm.Annotations().GetFieldAnnotation(meta.field, c.arguments[2].ref));
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
        "getSignatureAnnotation", "(Ljava/lang/Class;I)[Ljava/lang/Object;",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 0),
                                                               static_cast<std::uint32_t>(c.arguments[1].AsInt()));
            return SignatureFragments(c.vm, c.vm.Linker().Field(meta.field).generic_signature);
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
        "setField", "(Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/Class;IZLjava/lang/Object;)V",
        [](IntrinsicContext& c) {
            const auto& meta = c.vm.Reflection().FieldMetadata(ReflectionClass(c, 1),
                                                               static_cast<std::uint32_t>(c.arguments[3].AsInt()));
            c.vm.Reflection().SetField(meta, c.arguments[0].ref, c.arguments[5], std::nullopt, ReflectionCaller(c.vm),
                                       c.arguments[4].AsInt() != 0);
            return VmValue::Void();
        },
        kAccPrivate | kAccNative);
    return std::move(b).Build();
}

IntrinsicClassDecl Declare_java_lang_reflect_Method_Natives() {
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
