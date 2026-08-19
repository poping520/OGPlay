#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceView(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceView;", "Landroid/view/View;");
    builder.Constructor("(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.FinalMethod("getHolder", "()Landroid/view/SurfaceHolder;",
        [context](dx::IntrinsicContext& call) {
            auto& holder = context->surface_holders[call.receiver.Value()];
            if (!holder.IsValid()) {
                holder = call.vm.NewIntrinsicInstance(
                    "Landroid/view/SurfaceHolder$Impl;");
            }
            return dx::VmValue::Ref(holder);
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
