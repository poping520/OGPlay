#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_SurfaceHolder_Callback(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/SurfaceHolder$Callback;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
