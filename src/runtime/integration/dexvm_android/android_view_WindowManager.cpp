#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManager(const Context& context) {
    dx::IntrinsicClassBuilder builder("Landroid/view/WindowManager;");
    builder.MarkInterface();
    builder.Virtual("getDefaultDisplay", "()Landroid/view/Display;", WindowmanagerGetDefaultDisplayHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
