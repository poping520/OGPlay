#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Impl(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/SurfaceHolder$Impl;");
    builder.Super("Ljava/lang/Object;");
    builder.Implements("Landroid/view/SurfaceHolder;");
    builder.Virtual("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.Virtual("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.Virtual("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
