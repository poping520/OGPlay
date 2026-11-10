#include "android_skia.h"

namespace ogplay::runtime::android_intrinsics {
using namespace skia;
using V = dx::VmValue;

Decl Declare_android_graphics_DrawFilter(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/DrawFilter;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeDestructor", "(I)V",
        [context](dx::IntrinsicContext& c) {
            context->skia_objects->draw_filters.Retire(
                NativeToken<std::shared_ptr<raster::DrawFilterState>>(c.arguments[0].AsInt()));
            return V::Void();
        },
        kStaticNative);
    return std::move(b).Build();
}

Decl Declare_android_graphics_PaintFlagsDrawFilter(const Context& context) {
    auto b = dx::IntrinsicClassBuilder::Class("Landroid/graphics/PaintFlagsDrawFilter;");
    b.MergesBootClass();
    b.StaticMethod(
        "nativeConstructor", "(II)I",
        [context](dx::IntrinsicContext& c) {
            const auto clear = (c.arguments[0].AsInt() & 0xffff), set = (c.arguments[1].AsInt() & 0xffff);
            if (!(clear | set)) return V::Int(0);
            return JavaToken(context->skia_objects->draw_filters.Create(
                std::make_shared<raster::DrawFilterState>(raster::DrawFilterState{clear, set})));
        },
        kStaticNative);
    return std::move(b).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
