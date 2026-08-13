#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Bitmap(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Bitmap;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("createBitmap", "([IIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", handlers.handler_android_bitmap_create);
    builder.Static("createBitmap", "([IIIIILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;", handlers.handler_android_bitmap_create_offset);
    builder.Virtual("getWidth", "()I", handlers.handler_android_bitmap_get_width);
    builder.Virtual("getHeight", "()I", handlers.handler_android_bitmap_get_height);
    builder.Virtual("getPixels", "([IIIIIII)V", handlers.handler_android_bitmap_get_pixels);
    builder.Virtual("prepareToDraw", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("recycle", "()V", handlers.handler_android_bitmap_recycle);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
