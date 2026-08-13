#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/ImageView$ScaleType;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("CENTER", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Clinit([](dx::IntrinsicContext& call) {
        call.vm.SetIntrinsicStaticRef(
            "Landroid/widget/ImageView$ScaleType;", "CENTER",
            "Landroid/widget/ImageView$ScaleType;",
            call.vm.NewIntrinsicInstance(
                "Landroid/widget/ImageView$ScaleType;"));
        return dx::VmValue::Void();
    });
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
