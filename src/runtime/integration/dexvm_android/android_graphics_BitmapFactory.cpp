#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_BitmapFactory(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/BitmapFactory;");
    builder.Super("Ljava/lang/Object;");
    builder.Static("decodeByteArray", "([BII)Landroid/graphics/Bitmap;", handlers.handler_android_bitmap_factory_decode_byte_array);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
