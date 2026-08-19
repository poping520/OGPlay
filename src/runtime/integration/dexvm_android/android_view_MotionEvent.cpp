// Motion events read their slots directly.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_MotionEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/MotionEvent;", "Ljava/lang/Object;");
    builder.InstanceField("action", "I");
    builder.InstanceField("x", "F");
    builder.InstanceField("y", "F");
    builder.InstanceField("pointer", "I");
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
    builder.FinalMethod("getAction", "()I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits));
        });
    builder.FinalMethod("getX", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.FinalMethod("getY", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.FinalMethod("getX", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.FinalMethod("getY", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.FinalMethod("getPointerCount", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.FinalMethod("getPointerId", "(I)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[3].bits));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
