#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_view_KeyEvent(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/view/KeyEvent;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
