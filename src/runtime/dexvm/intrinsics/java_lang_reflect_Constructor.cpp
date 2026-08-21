#include "catalog.h"

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
         "Ljava/lang/reflect/Member;"}, 0x0011U);
    builder.InstanceField("declaringClass", "Ljava/lang/Class;", 0U);
    builder.InstanceField("parameterTypes", "[Ljava/lang/Class;", 0U);
    builder.InstanceField("exceptionTypes", "[Ljava/lang/Class;", 0U);
    builder.InstanceField("slot", "I", 0U);
    builder.InstanceField("methodDexIndex", "I", 0x0002U);

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
                .access_flags & 0x0007U));
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
                 .access_flags & 0x1000U) != 0U ? 1 : 0);
    });
    builder.VirtualMethod("equals", "(Ljava/lang/Object;)Z",
        [](IntrinsicContext& context) {
            return VmValue::Int(context.vm.Reflection().SemanticallyEqual(
                                    context.receiver,
                                    context.arguments[0].ref) ? 1 : 0);
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
