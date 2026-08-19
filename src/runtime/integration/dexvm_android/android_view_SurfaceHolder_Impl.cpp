#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/SurfaceHolder$Impl;", "Ljava/lang/Object;", {"Landroid/view/SurfaceHolder;"});
    builder.FinalMethod("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.FinalMethod("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.FinalMethod("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
