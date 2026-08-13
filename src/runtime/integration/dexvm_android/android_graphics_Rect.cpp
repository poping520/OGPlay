#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Rect(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Rect;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("left", "I", false);
    builder.Field("top", "I", false);
    builder.Field("right", "I", false);
    builder.Field("bottom", "I", false);
    builder.Virtual("<init>", "()V", GraphicsNoopHandler());
    builder.Virtual("width", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[2].bits) -
                                static_cast<std::int32_t>(slots[0].bits));
    });
    builder.Virtual("height", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[3].bits) -
                                static_cast<std::int32_t>(slots[1].bits));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
