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
    builder.InstanceField("target", "Ljava/lang/Throwable;", 0x0002U);
    builder.Constructor("()V", [](IntrinsicContext&) {
        return VmValue::Void();
    }, 0x0004U);
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
