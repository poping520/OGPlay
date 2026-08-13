#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Bitmap_Config(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Bitmap$Config;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("ARGB_4444", "Landroid/graphics/Bitmap$Config;", true);
    builder.Field("ARGB_8888", "Landroid/graphics/Bitmap$Config;", true);
    builder.Clinit(handlers.handler_android_bitmap_config_clinit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
