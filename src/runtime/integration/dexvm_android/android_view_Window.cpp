#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_Window(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/Window;", "Ljava/lang/Object;");
    builder.FinalMethod("setFlags", "(II)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("addFlags", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("clearFlags", "(I)V",
        [](dx::IntrinsicContext&) { return dx::VmValue::Void(); });
    builder.FinalMethod("getAttributes",
        "()Landroid/view/WindowManager$LayoutParams;",
        [context](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                Singleton(call, context, "window_attributes",
                          "Landroid/view/WindowManager$LayoutParams;"));
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
