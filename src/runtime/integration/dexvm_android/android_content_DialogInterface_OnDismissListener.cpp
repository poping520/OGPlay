#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnDismissListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnDismissListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
