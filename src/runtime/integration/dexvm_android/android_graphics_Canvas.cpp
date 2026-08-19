#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Canvas(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/graphics/Canvas;", "Ljava/lang/Object;");
    builder.FinalMethod("save", "(I)I", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    builder.FinalMethod("restore", "()V", GraphicsNoopHandler());
    builder.FinalMethod("clipRect", "(FFFFLandroid/graphics/Region$Op;)Z",
        [](dx::IntrinsicContext&) {
            return dx::VmValue::Int(1);
        });
    builder.FinalMethod("getClipBounds", "()Landroid/graphics/Rect;",
        [context](dx::IntrinsicContext& call) {
            const auto rect =
                call.vm.NewIntrinsicInstance("Landroid/graphics/Rect;");
            const auto slots = call.vm.Model().InstanceSlots(rect);
            slots[0] = {0, dx::SlotTag::cat1};
            slots[1] = {0, dx::SlotTag::cat1};
            slots[2] = {context->surface_width, dx::SlotTag::cat1};
            slots[3] = {context->surface_height, dx::SlotTag::cat1};
            return dx::VmValue::Ref(rect);
        });
    builder.FinalMethod("drawColor", "(I)V", GraphicsNoopHandler());
    builder.FinalMethod("drawBitmap", "(Landroid/graphics/Bitmap;FFLandroid/graphics/Paint;)V", GraphicsNoopHandler());
    builder.FinalMethod("drawBitmap", "([IIIIIIIZLandroid/graphics/Paint;)V", GraphicsNoopHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
