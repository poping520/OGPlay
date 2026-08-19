#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_WindowManagerImpl(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/WindowManagerImpl;", "Ljava/lang/Object;", {"Landroid/view/WindowManager;"});
    builder.FinalMethod("getDefaultDisplay", "()Landroid/view/Display;", WindowmanagerGetDefaultDisplayHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
