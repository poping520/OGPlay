#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Configuration(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/content/res/Configuration;");
    builder.Super("Ljava/lang/Object;");
    builder.Field("keyboard", "I", false);
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
