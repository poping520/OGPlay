#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_graphics_Matrix(const Context& context) {
    const auto handlers = MakeAndroidHandlers(context);
    dx::IntrinsicClassBuilder builder("Landroid/graphics/Matrix;");
    builder.Super("Ljava/lang/Object;");
    builder.Virtual("<init>", "()V", handlers.handler_android_graphics_noop);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
