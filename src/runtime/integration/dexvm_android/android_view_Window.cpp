#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {
namespace {

[[nodiscard]] dx::VmObjectRef Attributes(dx::IntrinsicContext& call,
                                         const Context& context) {
    return Singleton(call, context, "window_attributes",
                     "Landroid/view/WindowManager$LayoutParams;");
}

[[nodiscard]] const dx::LinkedField& IntField(dx::IntrinsicContext& call,
                                              const dx::VmObjectRef object,
                                              const std::string& name) {
    const auto field = call.vm.Linker().FindFieldRecursive(
        call.vm.Model().ObjectClass(object), name, "I");
    if (!field.has_value()) {
        throw dx::DexVmError(dx::DexVmErrorReason::internal_invariant,
                             "Window LayoutParams field is missing: " + name);
    }
    return call.vm.Linker().Field(*field);
}

[[nodiscard]] std::int32_t ReadIntField(dx::IntrinsicContext& call,
                                        const dx::VmObjectRef object,
                                        const std::string& name) {
    const auto& field = IntField(call, object, name);
    return static_cast<std::int32_t>(
        call.vm.Model().InstanceSlots(object)[field.slot].bits);
}

void WriteIntField(dx::IntrinsicContext& call, const dx::VmObjectRef object,
                   const std::string& name, const std::int32_t value) {
    const auto& field = IntField(call, object, name);
    call.vm.Model().InstanceSlots(object)[field.slot] = {
        static_cast<std::uint32_t>(value), dx::SlotTag::cat1};
}

}  // namespace

Decl Declare_android_view_Window(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Window;", "Ljava/lang/Object;");
    builder.FinalMethod("setFlags", "(II)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            const auto flags = call.arguments[0].AsInt();
            const auto mask = call.arguments[1].AsInt();
            const auto old = ReadIntField(call, attributes, "flags");
            WriteIntField(call, attributes, "flags",
                          (old & ~mask) | (flags & mask));
            return dx::VmValue::Void();
        });
    builder.FinalMethod("addFlags", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            const auto flags = call.arguments[0].AsInt();
            WriteIntField(call, attributes, "flags",
                          ReadIntField(call, attributes, "flags") | flags);
            return dx::VmValue::Void();
        });
    builder.FinalMethod("clearFlags", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto attributes = Attributes(call, context);
            WriteIntField(call, attributes, "flags",
                          ReadIntField(call, attributes, "flags") &
                              ~call.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setSoftInputMode", "(I)V",
        [context](dx::IntrinsicContext& call) {
            const auto mode = call.arguments[0].AsInt();
            if (mode != 0) {
                WriteIntField(call, Attributes(call, context),
                              "softInputMode", mode);
            }
            return dx::VmValue::Void();
        });
    builder.FinalMethod("setType", "(I)V",
        [context](dx::IntrinsicContext& call) {
            WriteIntField(call, Attributes(call, context), "type",
                          call.arguments[0].AsInt());
            return dx::VmValue::Void();
        });
    builder.FinalMethod("getAttributes",
        "()Landroid/view/WindowManager$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(Attributes(call, context));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
