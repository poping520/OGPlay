#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_widget_ImageView_ScaleType(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/widget/ImageView$ScaleType;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("CENTER", "Landroid/widget/ImageView$ScaleType;", true);
    builder.Clinit(handlers.handler_android_scale_type_clinit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
