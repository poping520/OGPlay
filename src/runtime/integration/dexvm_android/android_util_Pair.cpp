#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_util_Pair(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/util/Pair;", "Ljava/lang/Object;");
    builder.InstanceField("first", "Ljava/lang/Object;");
    builder.InstanceField("second", "Ljava/lang/Object;");
    builder.Constructor("(Ljava/lang/Object;Ljava/lang/Object;)V",
        [](dx::IntrinsicContext& call) {
            const auto slots = call.vm.Model().InstanceSlots(call.receiver);
            slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
            slots[1] = {call.arguments[1].ref.Value(), dx::SlotTag::ref};
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
