// Toast has no on-screen surface here; show() lands in the guest log.

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_Toast(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/widget/Toast;", "Ljava/lang/Object;");
    builder.StaticMethod("makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)"
        "Landroid/widget/Toast;",
        [](dx::IntrinsicContext& call) {
            return dx::VmValue::Ref(
                call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
        });
    builder.FinalMethod("show", "()V",
        [](dx::IntrinsicContext& call) {
            GuestLog(call, core::LogLevel::info, "Toast.show()");
            return dx::VmValue::Void();
        });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
