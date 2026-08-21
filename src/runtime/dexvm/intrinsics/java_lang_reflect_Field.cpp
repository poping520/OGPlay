#include "catalog.h"

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
        {"Ljava/lang/reflect/Member;"}, 0x0011U);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", 0x0002U);
    builder.InstanceField("type", "Ljava/lang/Class;", 0x0002U);
    builder.InstanceField("name", "Ljava/lang/String;", 0x0002U);
    builder.InstanceField("slot", "I", 0x0002U);
    builder.InstanceField("fieldDexIndex", "I", 0x0012U);

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
                .access_flags & 0x00dfU));
    });
    builder.VirtualMethod("isSynthetic", "()Z", [](IntrinsicContext& context) {
        return VmValue::Int(
            (context.vm.Reflection().FieldMetadata(context.receiver)
                 .access_flags & 0x1000U) != 0U ? 1 : 0);
    });
    builder.VirtualMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
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
