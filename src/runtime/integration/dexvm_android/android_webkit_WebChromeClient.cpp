#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_webkit_WebChromeClient(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/webkit/WebChromeClient;", "Ljava/lang/Object;");
    builder.Constructor("()V", NeutralHandler('V'));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
