// Motion events read their slots directly.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_MotionEvent(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/view/MotionEvent;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("action", "I", false);
    builder.Field("x", "F", false);
    builder.Field("y", "F", false);
    builder.Field("pointer", "I", false);
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
    builder.Virtual("getAction", "()I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[0].bits));
        });
    builder.Virtual("getX", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.Virtual("getY", "()F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.Virtual("getX", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 1);
        });
    builder.Virtual("getY", "(I)F",
        [slot_float](dx::IntrinsicContext& call) {
            return slot_float(call, 2);
        });
    builder.Virtual("getPointerCount", "()I",
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(1); });
    builder.Virtual("getPointerId", "(I)I",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Int(static_cast<std::int32_t>(
                call.vm.Model().InstanceSlots(call.receiver)[3].bits));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
