#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Pair(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/util/Pair;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("first", "Ljava/lang/Object;", false);
    builder.Field("second", "Ljava/lang/Object;", false);
    builder.Virtual("<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
            slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
