#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_res_Configuration(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/res/Configuration;", "Ljava/lang/Object;");
    builder.InstanceField("keyboard", "I");
    builder.InstanceField("screenLayout", "I");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
