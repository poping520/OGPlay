#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_View_OnTouchListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/view/View$OnTouchListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
