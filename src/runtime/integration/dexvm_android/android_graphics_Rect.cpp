#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Rect(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Rect;", "Ljava/lang/Object;");
    builder.InstanceField("left", "I");
    builder.InstanceField("top", "I");
    builder.InstanceField("right", "I");
    builder.InstanceField("bottom", "I");
    builder.Constructor("()V", GraphicsNoopHandler());
    builder.FinalMethod("width", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[2].bits) -
                                static_cast<std::int32_t>(slots[0].bits));
    });
    builder.FinalMethod("height", "()I", [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        return dx::VmValue::Int(static_cast<std::int32_t>(slots[3].bits) -
                                static_cast<std::int32_t>(slots[1].bits));
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
