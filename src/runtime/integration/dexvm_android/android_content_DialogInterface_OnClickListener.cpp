#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_DialogInterface_OnClickListener(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/DialogInterface$OnClickListener;");
    builder.MarkInterface();
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
