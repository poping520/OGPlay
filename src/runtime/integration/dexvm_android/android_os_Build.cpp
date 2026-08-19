#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_android_os_Build(const Context& context) {
    static_cast<void>(context);
    auto builder = dx::IntrinsicClassBuilder::Class("Landroid/os/Build;", "Ljava/lang/Object;");
    builder.ConstantString("CPU_ABI", "armeabi");
    builder.ConstantString("DEVICE", "unknown");
    builder.ConstantString("MANUFACTURER", "unknown");
    builder.ConstantString("MODEL", "unknown");
    builder.ConstantString("PRODUCT", "unknown");
    builder.ConstantString("TAGS", "release-keys");
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
