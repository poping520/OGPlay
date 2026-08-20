#include "catalog.h"

#include <cstdint>

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
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::dexvm::intrinsics
