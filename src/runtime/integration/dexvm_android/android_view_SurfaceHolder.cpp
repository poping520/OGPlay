#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/SurfaceHolder;");
    builder.MarkInterface();
    builder.Virtual("addCallback", "(Landroid/view/SurfaceHolder$Callback;)V", SurfaceHolderAddCallbackHandler(context));
    builder.Virtual("setType", "(I)V", SurfaceHolderSetTypeHandler());
    builder.Virtual("setFormat", "(I)V", SurfaceHolderSetFormatHandler());
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
