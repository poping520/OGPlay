#include "catalog.h"
#include "shared.h"

#include <cstdint>
#include <string_view>

#include "ogplay/runtime/dexvm/class_name_codec.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/interpreter.h"
#include "ogplay/runtime/dexvm/reflection.h"

namespace ogplay::runtime::dexvm::intrinsics {
constexpr std::uint32_t kMethodModifierMask = 0x0d3fU;

IntrinsicClassDecl Declare_java_lang_reflect_Method() {
    auto builder = IntrinsicClassBuilder::Class(
        "Ljava/lang/reflect/Method;", "Ljava/lang/reflect/AccessibleObject;",
        {"Ljava/lang/reflect/GenericDeclaration;",
         "Ljava/lang/reflect/Member;"}, 0x0011U);
    builder.InstanceField("slot", "I", 0x0002U);
    builder.InstanceField("methodDexIndex", "I", 0x0012U);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", 0x0002U);
    builder.InstanceField("name", "Ljava/lang/String;", 0x0002U);
    builder.InstanceField("parameterTypes", "[Ljava/lang/Class;", 0x0002U);
    builder.InstanceField("exceptionTypes", "[Ljava/lang/Class;", 0x0002U);
    builder.InstanceField("returnType", "Ljava/lang/Class;", 0x0002U);

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
                .access_flags & kMethodModifierMask));
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
                 .access_flags & 0x1000U) != 0U ? 1 : 0);
    });
    builder.VirtualMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
        });
    builder.VirtualMethod("hashCode", "()I", [](IntrinsicContext& context) {
        const auto& meta = context.vm.Reflection().MethodMetadata(
            context.receiver);
        const auto& method = context.vm.Linker().Method(meta.method);
        return VmValue::Int(detail::JavaUtf8Hash(context, method.name));
    });
    builder.VirtualMethod("toString", "()Ljava/lang/String;",
        [](IntrinsicContext& context) {
            const auto& meta = context.vm.Reflection().MethodMetadata(
                context.receiver);
            const auto& linker = context.vm.Linker();
            const auto& method = linker.Method(meta.method);
            auto text = detail::ModifierString(meta.access_flags &
                                               kMethodModifierMask);
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
