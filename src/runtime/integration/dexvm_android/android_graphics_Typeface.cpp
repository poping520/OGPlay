#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Typeface(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Typeface;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("SERIF", "Landroid/graphics/Typeface;", true);
    builder.Static("defaultFromStyle", "(I)Landroid/graphics/Typeface;", handlers.handler_android_typeface_default_from_style);
    builder.Clinit(handlers.handler_android_typeface_clinit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
