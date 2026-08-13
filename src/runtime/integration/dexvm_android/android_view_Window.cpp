#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Window(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/Window;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("setFlags", "(II)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("addFlags", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("clearFlags", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.Virtual("getAttributes",
        "()Landroid/view/WindowManager$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window_attributes",
                          "Landroid/view/WindowManager$LayoutParams;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
