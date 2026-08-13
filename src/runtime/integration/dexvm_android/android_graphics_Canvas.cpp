#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Canvas(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Canvas;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("save", "(I)I", handlers.handler_android_canvas_save);
    builder.Virtual("restore", "()V", handlers.handler_android_graphics_noop);
    builder.Virtual("clipRect", "(FFFFLandroid/graphics/Region$Op;)Z", handlers.handler_android_canvas_clip_rect);
    builder.Virtual("getClipBounds", "()Landroid/graphics/Rect;", handlers.handler_android_canvas_get_clip_bounds);
    builder.Virtual("drawColor", "(I)V", handlers.handler_android_graphics_noop);
    builder.Virtual("drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", handlers.handler_android_graphics_noop);
    builder.Virtual("drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", handlers.handler_android_graphics_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
