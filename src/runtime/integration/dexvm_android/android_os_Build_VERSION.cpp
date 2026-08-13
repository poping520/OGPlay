#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Build_VERSION(const Context& context) {
    static_cast<void>(context);
    dx::IntrinsicClassBuilder builder("Landroid/os/Build$VERSION;");
    builder.Super("Ljava/lang/Object;");
    builder.ConstantInt("SDK_INT", "I", 19);
    builder.ConstantString("SDK", "19");
    builder.ConstantString("RELEASE", "4.4.4");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
