#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_text_TextPaint(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/text/TextPaint;");
    builder.Super("Landroid/graphics/Paint;");
    builder.Virtual("getTextBounds", "(Ljava/lang/String;IILandroid/graphics/Rect;)V", handlers.handler_android_paint_get_text_bounds);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
