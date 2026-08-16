#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceView(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/SurfaceView;");
    builder.Super("Landroid/view/View;");
    builder.Virtual("<init>", "(Landroid/content/Context;)V", ViewInitHandler(context));
    builder.Virtual("getHolder", "()Landroid/view/SurfaceHolder;",
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
