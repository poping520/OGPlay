#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Region_Op(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Region$Op;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("REPLACE", "Landroid/graphics/Region$Op;", true);
    builder.Clinit(handlers.handler_android_region_op_clinit);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
