#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Display(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Display;", "Ljava/lang/Object;");
    builder.FinalMethod("getWidth", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_width));
        });
    builder.FinalMethod("getHeight", "()I",
        [context](dx::IntrinsicContext&) {
            return dx::VmValue::Int(
                static_cast<std::int32_t>(context->surface_height));
        });
    // Managed surface coordinates are landscape-natural and never
    // rotate independently from the host window.
    const auto get_rotation = dx::IntrinsicHandler(
        [](dx::IntrinsicContext&) { return dx::VmValue::Int(0); });
    builder.FinalMethod("getRotation", "()I", get_rotation);
    builder.FinalMethod("getOrientation", "()I", get_rotation);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
