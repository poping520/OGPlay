#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Interface("Landroid/content/DialogInterface$OnClickListener;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
