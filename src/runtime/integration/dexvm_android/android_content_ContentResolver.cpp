#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_content_ContentResolver(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/content/ContentResolver;", "Ljava/lang/Object;");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
