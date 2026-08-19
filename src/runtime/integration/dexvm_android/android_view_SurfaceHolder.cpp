#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder;");
    builder.FinalMethod("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.FinalMethod("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.FinalMethod("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
